/* Original single-process GSP direct-framebuffer bridge from public wire facts. */
#include "ctr_gsp.h"
#include <string.h>
static unsigned pixel_bytes(unsigned format) { return format==0?4:format==1?3:2; }
static bool supported(unsigned screen,const uint32_t *info) {
    unsigned format=info[4]&7u;
    return screen<2 && info[0]<=1 && info[5]==info[0] && !info[6] && format<=4 &&
        !(info[4]&~0x147u) && (info[4]&0x100u) && ((info[4]&0x40u)!=0)==(screen==0) &&
        (!info[2] || info[2]==info[1]) && info[3]>=240*pixel_bytes(format) && info[3]<=4096;
}
static ctr_mem_result source_ok(const ctr_guest *g,unsigned screen,const uint32_t *info) {
    unsigned width=screen?320:400;
    uint32_t size=(width-1)*info[3]+240*pixel_bytes(info[4]&7u);
    return ctr_memory_check_range(&g->memory,info[1],size,CTR_MEM_READ);
}
bool ctr_gsp_request(ctr_guest *g,const uint32_t *words,uint32_t *out,unsigned *count) {
    *count=2;out[0]=(words[0]&0xffff0000u)|0x40u;out[1]=0;
    switch(words[0]) {
        case 0x00160042: /* AcquireRight, flags0, one copied current-process handle. */
            if(words[1] || words[2] || words[3]!=0xffff8001u)return false;
            g->gsp.right=true;return true;
        case 0x00170000:
            if(!g->gsp.right)return false;
            g->gsp.right=false;return true;
        case 0x000b0040:
            if(!g->gsp.right || words[1]>1)return false;
            g->gsp.black=words[1]!=0;return true;
        case 0x00050200: {
            if(!g->gsp.right || !supported(words[1],words+2))return false;
            ctr_mem_result result=source_ok(g,words[1],words+2);
            if(result!=CTR_MEM_OK) {
                g->cpu.memory_fault=result;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=words[3];
                g->state=CTR_GUEST_FAULTED;ctr_cpu_stop(&g->cpu,g->fault,g->fault_detail);return false;
            }
            memcpy(g->gsp.info[words[1]],words+2,sizeof(g->gsp.info[0]));
            g->gsp.active[words[1]]=true;++g->gsp.swaps[words[1]];return true;
        }
        default:return false;
    }
}
static unsigned expand(unsigned value,unsigned bits) {
    return bits==6?(value<<2)|(value>>4):bits==5?(value<<3)|(value>>2):(value<<4)|value;
}
ctr_mem_result ctr_gsp_pixels(const ctr_guest *g,unsigned screen,uint32_t *rgba,size_t count) {
    if(screen>1 || !rgba || count!=(screen?320u:400u)*240u || !g->gsp.active[screen])return CTR_MEM_UNMAPPED;
    if(g->gsp.black) { for(size_t i=0;i<count;++i)rgba[i]=0x000000ff;return CTR_MEM_OK; }
    const uint32_t *info=g->gsp.info[screen];ctr_mem_result result=source_ok(g,screen,info);
    if(result!=CTR_MEM_OK)return result;
    unsigned format=info[4]&7u,bpp=pixel_bytes(format),width=screen?320:400;
    for(unsigned x=0;x<width;++x)for(unsigned y=0;y<240;++y) {
        uint32_t address=info[1]+x*info[3]+(239-y)*bpp,raw=0,byte;
        for(unsigned i=0;i<bpp;++i) {
            result=ctr_memory_read(&g->memory,address+i,1,CTR_MEM_READ,&byte);
            if(result!=CTR_MEM_OK)return result;
            raw|=byte<<(8*i);
        }
        unsigned r,gc,b,a=255;
        if(format==0) { r=raw>>24;gc=(raw>>16)&255;b=(raw>>8)&255;a=raw&255; }
        else if(format==1) { r=(raw>>16)&255;gc=(raw>>8)&255;b=raw&255; }
        else if(format==2) { r=expand(raw>>11,5);gc=expand((raw>>5)&63,6);b=expand(raw&31,5); }
        else if(format==3) { r=expand(raw>>11,5);gc=expand((raw>>6)&31,5);b=expand((raw>>1)&31,5);a=(raw&1)?255:0; }
        else { r=expand(raw>>12,4);gc=expand((raw>>8)&15,4);b=expand((raw>>4)&15,4);a=expand(raw&15,4); }
        rgba[y*width+x]=(r<<24)|(gc<<16)|(b<<8)|a;
    }
    return CTR_MEM_OK;
}
