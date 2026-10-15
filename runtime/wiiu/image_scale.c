#include "image_scale.h"
#include <stdlib.h>
#include <string.h>

bool ctr_scale_plan_init(ctr_scale_plan *p,const CtrViewport *v) {
    if(!p || !v || p->columns || p->rows || !v->width || !v->height ||
        !v->guest_width || !v->guest_height || v->guest_width>UINT16_MAX ||
        v->guest_height>UINT16_MAX || v->output_width>8192 || v->output_height>8192 ||
        v->x>v->output_width || v->width>v->output_width-v->x ||
        v->y>v->output_height || v->height>v->output_height-v->y)return false;
    p->columns=malloc((size_t)v->width*sizeof(*p->columns));
    p->rows=malloc((size_t)v->height*sizeof(*p->rows));
    if(!p->columns || !p->rows) { ctr_scale_plan_close(p);return false; }
    p->view=*v;
    for(uint32_t x=0;x<v->width;++x)p->columns[x]=(uint16_t)((uint64_t)x*v->guest_width/v->width);
    for(uint32_t y=0;y<v->height;++y)p->rows[y]=(uint16_t)((uint64_t)y*v->guest_height/v->height);
    return true;
}
void ctr_scale_plan_close(ctr_scale_plan *p) {
    if(!p)return;
    free(p->columns);free(p->rows);memset(p,0,sizeof(*p));
}
bool ctr_scale_rgba(const ctr_scale_plan *p,const uint32_t *image,size_t image_pixels,unsigned char *out,
                    size_t bytes,uint32_t pitch) {
    if(!p || !p->columns || !p->rows || !image || !out)return false;
    const CtrViewport *v=&p->view;
    if((uint64_t)v->guest_width*v->guest_height>image_pixels ||
        pitch<v->output_width || (uint64_t)pitch*v->output_height>SIZE_MAX/4 ||
        bytes<(size_t)pitch*v->output_height*4)return false;
    memset(out,0,bytes);
    unsigned char *previous=NULL;uint16_t previous_y=UINT16_MAX;
    for(uint32_t y=0;y<v->height;++y) {
        unsigned char *row=out+((size_t)(v->y+y)*pitch+v->x)*4;
        uint16_t sy=p->rows[y];
        if(previous && sy==previous_y)memcpy(row,previous,(size_t)v->width*4);
        else {
            const uint32_t *source=image+(size_t)sy*v->guest_width;
            for(uint32_t x=0;x<v->width;++x) {
                uint32_t rgba=source[p->columns[x]];
                row[x*4]=(unsigned char)(rgba>>24);row[x*4+1]=(unsigned char)(rgba>>16);
                row[x*4+2]=(unsigned char)(rgba>>8);row[x*4+3]=(unsigned char)rgba;
            }
        }
        previous=row;previous_y=sy;
    }
    return true;
}

static bool same_view(const CtrViewport *a,const CtrViewport *b) {
    return a->output_width==b->output_width && a->output_height==b->output_height &&
        a->x==b->x && a->y==b->y && a->width==b->width && a->height==b->height &&
        a->guest_width==b->guest_width && a->guest_height==b->guest_height;
}
static bool disjoint(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an<=UINTPTR_MAX-x && bn<=UINTPTR_MAX-y && (x+an<=y || y+bn<=x);
}
bool ctr_scale_cache_init(ctr_scale_cache *c,const ctr_scale_plan *p) {
    if(!c || !p || c->previous || c->ranges || !p->columns || !p->rows)return false;
    uint64_t pixels=(uint64_t)p->view.guest_width*p->view.guest_height;
    if(!pixels || pixels>CTR_SCALE_CACHE_MAX_PIXELS || !p->view.height ||
       p->view.height>8192 || pixels>SIZE_MAX/sizeof(*c->previous))return false;
    uint32_t *previous=malloc((size_t)pixels*sizeof(*previous));
    ctr_scale_range *ranges=malloc((size_t)p->view.height*sizeof(*ranges));
    if(!previous || !ranges) { free(previous);free(ranges);return false; }
    *c=(ctr_scale_cache){.plan=p,.view=p->view,.previous=previous,.ranges=ranges,
        .source_pixels=(size_t)pixels,.range_capacity=p->view.height};
    return true;
}
void ctr_scale_cache_invalidate(ctr_scale_cache *c) { if(c)c->valid=false; }
void ctr_scale_cache_close(ctr_scale_cache *c) {
    if(!c)return;
    free(c->previous);free(c->ranges);memset(c,0,sizeof(*c));
}
/* First column whose nearest-neighbor source index is at least source_x. */
static uint32_t column_lower_bound(const ctr_scale_plan *p,uint32_t source_x) {
    uint32_t lo=0,hi=p->view.width;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;
        if(p->columns[mid]<source_x)lo=mid+1;else hi=mid;
    }
    return lo;
}
bool ctr_scale_cache_update(ctr_scale_cache *c,const uint32_t *image,size_t pixels,
                           unsigned char *out,size_t bytes,uint32_t pitch,ctr_scale_update *update) {
    if(!c || !c->plan || !c->previous || !c->ranges || !image || !out || !update)return false;
    const ctr_scale_plan *p=c->plan;const CtrViewport *v=&c->view;
    if(!p->columns || !p->rows || !same_view(v,&p->view) || !v->height ||
       c->range_capacity<v->height || c->source_pixels!=(size_t)v->guest_width*v->guest_height ||
       c->source_pixels>CTR_SCALE_CACHE_MAX_PIXELS || pixels<c->source_pixels ||
       pitch<v->output_width || (uint64_t)pitch*v->output_height>SIZE_MAX/4 ||
       bytes<(size_t)pitch*v->output_height*4 ||
       !disjoint(image,c->source_pixels*4,out,bytes) ||
       !disjoint(c->previous,c->source_pixels*4,out,bytes) ||
       !disjoint(c->previous,c->source_pixels*4,image,c->source_pixels*4))return false;
    ctr_scale_update r={0};
    if(!c->valid || c->output!=out || c->output_bytes!=bytes || c->pitch!=pitch) {
        if(!ctr_scale_rgba(p,image,pixels,out,bytes,pitch))return false;
        c->range_count=1;c->ranges[0]=(ctr_scale_range){0,bytes};
        r=(ctr_scale_update){true,v->guest_height,v->height,(uint64_t)v->width*v->height,bytes};
    } else {
        c->range_count=0;
        if(!memcmp(c->previous,image,c->source_pixels*4)) { *update=r;return true; }
        for(uint32_t y=0;y<v->height;) {
            uint32_t sy=p->rows[y],end=y+1;
            while(end<v->height && p->rows[end]==sy)++end;
            const uint32_t *row=image+(size_t)sy*v->guest_width;
            const uint32_t *prior=c->previous+(size_t)sy*v->guest_width;
            uint32_t left=0,right=v->guest_width;
            while(left<right && row[left]==prior[left])++left;
            while(right>left && row[right-1]==prior[right-1])--right;
            uint32_t first=column_lower_bound(p,left),last=column_lower_bound(p,right);
            if(first<last) {
                unsigned char *target=out+((size_t)(v->y+y)*pitch+v->x+first)*4;
                for(uint32_t x=first;x<last;++x) {
                    uint32_t rgba=row[p->columns[x]];size_t at=(size_t)(x-first)*4;
                    target[at]=(unsigned char)(rgba>>24);target[at+1]=(unsigned char)(rgba>>16);
                    target[at+2]=(unsigned char)(rgba>>8);target[at+3]=(unsigned char)rgba;
                }
                for(uint32_t dy=y+1;dy<end;++dy)
                    memcpy(out+((size_t)(v->y+dy)*pitch+v->x+first)*4,target,(size_t)(last-first)*4);
                size_t offset=(size_t)(v->y+y)*pitch*4,length=(size_t)(end-y)*pitch*4;
                if(c->range_count && c->ranges[c->range_count-1].offset+c->ranges[c->range_count-1].bytes==offset)
                    c->ranges[c->range_count-1].bytes+=length;
                else c->ranges[c->range_count++]=(ctr_scale_range){offset,length};
                ++r.source_rows;r.output_rows+=end-y;r.scaled_pixels+=(uint64_t)(last-first)*(end-y);
                r.publish_bytes+=length;
            }
            y=end;
        }
    }
    memcpy(c->previous,image,c->source_pixels*4);
    c->output=out;c->output_bytes=bytes;c->pitch=pitch;c->valid=true;*update=r;
    return true;
}
