/* Original PICA format decoder from published layout facts. No emulator linked. */
#include "ctr_texture.h"
#include <stdbool.h>

static const unsigned bits[14]={32,24,16,16,16,16,16,8,8,8,4,4,4,8};
static uint32_t little32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint8_t expand(unsigned value,unsigned width) {
    return (uint8_t)(width==4?value*17:width==5?(value<<3)|(value>>2):(value<<2)|(value>>4));
}
static unsigned morton(unsigned x,unsigned y) {
    unsigned value=0;
    for(unsigned i=0;i<3;++i)value|=((x>>i)&1)<<(2*i)|((y>>i)&1)<<(2*i+1);
    return value;
}
static int delta3(unsigned value) { return (value&4)?(int)value-8:(int)value; }
static bool valid_etc(const uint8_t *p) {
    uint32_t high=little32(p+4);
    if(!(high&2))return true;
    for(unsigned shift=8;shift<=24;shift+=8) {
        int value=(int)((high>>(shift+3))&31)+delta3((high>>shift)&7);
        if(value<0 || value>31)return false;
    }
    return true;
}
static uint8_t clamp(int value) { return (uint8_t)(value<0?0:value>255?255:value); }
static void etc_pixel(const uint8_t *p,unsigned x,unsigned y,uint8_t *output) {
    static const int small[8]={2,5,9,13,18,24,33,47},large[8]={8,17,29,42,60,80,106,183};
    uint32_t low=little32(p),high=little32(p+4);
    bool first=(high&1)?y<2:x<2;
    unsigned table=(high>>(first?5:2))&7,index=x*4+y;
    int delta=((low>>index)&1)?large[table]:small[table];
    if((low>>(index+16))&1)delta=-delta;
    for(unsigned channel=0;channel<3;++channel) {
        unsigned shift=24-channel*8;int base;
        if(high&2) {
            base=(int)((high>>(shift+3))&31);
            if(!first)base+=delta3((high>>shift)&7);
            base=expand((unsigned)base,5);
        } else base=expand((high>>(shift+(first?4:0)))&15,4);
        output[channel]=clamp(base+delta);
    }
    output[3]=255;
}
ctr_texture_result ctr_texture_size(unsigned format,unsigned width,unsigned height,size_t *size) {
    if(format>=14)return CTR_TEXTURE_FORMAT;
    if(width<8 || height<8 || width>1024 || height>1024 || (width&(width-1)) || (height&(height-1)))return CTR_TEXTURE_DIMENSIONS;
    if(!size)return CTR_TEXTURE_SOURCE;
    *size=(size_t)width*height*bits[format]/8;return CTR_TEXTURE_OK;
}
ctr_texture_result ctr_texture_validate(const uint8_t *source,size_t source_bytes,unsigned format,
                                       unsigned width,unsigned height) {
    size_t need;ctr_texture_result result=ctr_texture_size(format,width,height,&need);
    if(result!=CTR_TEXTURE_OK)return result;
    if(!source || source_bytes<need)return CTR_TEXTURE_SOURCE;
    if(need>UINTPTR_MAX-(uintptr_t)source)return CTR_TEXTURE_ALIAS;
    if(format>=12) {
        unsigned block=format==12?8:16;
        for(size_t i=0;i<need;i+=block)if(!valid_etc(source+i+(format==13?8:0)))return CTR_TEXTURE_COMPRESSED;
    }
    return CTR_TEXTURE_OK;
}
ctr_texture_result ctr_texture_decode(const uint8_t *source,size_t source_bytes,unsigned format,
                                     unsigned width,unsigned height,uint8_t *output,size_t output_bytes) {
    size_t need;ctr_texture_result result=ctr_texture_size(format,width,height,&need);
    if(result!=CTR_TEXTURE_OK)return result;
    if(!source || source_bytes<need)return CTR_TEXTURE_SOURCE;
    size_t pixels=(size_t)width*height,out=pixels*4;
    if(!output || output_bytes<out)return CTR_TEXTURE_DESTINATION;
    uintptr_t start=(uintptr_t)source,end=(uintptr_t)output;
    if(need>UINTPTR_MAX-start || out>UINTPTR_MAX-end || (start<end+out && end<start+need))return CTR_TEXTURE_ALIAS;
    result=ctr_texture_validate(source,source_bytes,format,width,height);
    if(result!=CTR_TEXTURE_OK)return result;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        uint8_t *rgba=output+((size_t)y*width+x)*4;
        unsigned tile=(y/8)*(width/8)+x/8,texel=tile*64+morton(x&7,y&7);
        if(format>=12) {
            unsigned block=tile*4+(y%8)/4*2+(x%8)/4,stride=format==12?8:16;
            const uint8_t *p=source+(size_t)block*stride;etc_pixel(p+(format==13?8:0),x%4,y%4,rgba);
            if(format==13) {
                unsigned index=((x%4)*4+y%4)*4;
                rgba[3]=expand((p[index/8]>>(index%8))&15,4);
            }
            continue;
        }
        unsigned bit=texel*bits[format];const uint8_t *p=source+bit/8;
        rgba[0]=rgba[1]=rgba[2]=0;rgba[3]=255;
        uint32_t word=bits[format]==16?(uint32_t)p[0]|(uint32_t)p[1]<<8:0;
        switch(format) {
            case 0:rgba[0]=p[3];rgba[1]=p[2];rgba[2]=p[1];rgba[3]=p[0];break;
            case 1:rgba[0]=p[2];rgba[1]=p[1];rgba[2]=p[0];break;
            case 2:rgba[0]=expand(word>>11,5);rgba[1]=expand((word>>6)&31,5);rgba[2]=expand((word>>1)&31,5);rgba[3]=(word&1)?255:0;break;
            case 3:rgba[0]=expand(word>>11,5);rgba[1]=expand((word>>5)&63,6);rgba[2]=expand(word&31,5);break;
            case 4:rgba[0]=expand(word>>12,4);rgba[1]=expand((word>>8)&15,4);rgba[2]=expand((word>>4)&15,4);rgba[3]=expand(word&15,4);break;
            case 5:rgba[0]=rgba[1]=rgba[2]=p[1];rgba[3]=p[0];break;
            case 6:rgba[0]=p[1];rgba[1]=p[0];break;
            case 7:rgba[0]=rgba[1]=rgba[2]=p[0];break;
            case 8:rgba[3]=p[0];break;
            case 9:rgba[0]=rgba[1]=rgba[2]=expand(p[0]>>4,4);rgba[3]=expand(p[0]&15,4);break;
            case 10:rgba[0]=rgba[1]=rgba[2]=expand((p[0]>>(bit%8))&15,4);break;
            case 11:rgba[3]=expand((p[0]>>(bit%8))&15,4);break;
        }
    }
    return CTR_TEXTURE_OK;
}
