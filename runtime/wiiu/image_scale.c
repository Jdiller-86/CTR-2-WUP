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
