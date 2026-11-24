/* Original display-transfer conversion from wire/layout facts. */
#include "ctr_transfer.h"
#include <string.h>
static unsigned bpp(unsigned format) { return format==0?4:format==1?3:2; }
static unsigned morton(unsigned low) {
    static const uint8_t offsets[]={0,1,4,5,16,17,20,21};
    return offsets[low&7u];
}
static size_t row(unsigned y,unsigned width,bool tiled) {
    return tiled?(size_t)(y&~7u)*width+2u*morton(y):(size_t)y*width;
}
static unsigned column(unsigned x,bool tiled) {
    return tiled?(x&~7u)*8u+morton(x):x;
}
bool ctr_transfer_init(ctr_transfer *t,uint32_t in,uint32_t out,uint32_t flags) {
    unsigned input_width=in&65535,input_height=in>>16,width=out&65535,height=out>>16;
    unsigned input=(flags>>8)&7,output=(flags>>12)&7;
    if(!t || !width || !height || width>input_width || height>input_height ||
       input_width>1024 || input_height>1024 || input>4 || output>4 ||
       (flags&~UINT32_C(0x00007727)) || (in!=out && (flags&5u)!=4u))return false;
    bool linear=(flags&2)!=0,tiled=linear!=((flags&32)!=0);
    if((!linear && ((input_width|input_height)&7u)) || (tiled && ((width|height)&7u)))return false;
    *t=(ctr_transfer){width,height,input_width,input,output,linear,tiled,(flags&1)!=0,
                      (size_t)input_width*input_height*bpp(input),(size_t)width*height*bpp(output)};
    return true;
}
static void read_pixel(const uint8_t *p,unsigned format,uint8_t rgba[4]) {
    unsigned word=(unsigned)p[0]|(unsigned)p[1]<<8;rgba[3]=255;
    if(format==0) { rgba[0]=p[3];rgba[1]=p[2];rgba[2]=p[1];rgba[3]=p[0]; }
    else if(format==1) { rgba[0]=p[2];rgba[1]=p[1];rgba[2]=p[0]; }
    else if(format==2)ctr_rgb565_unpack((uint16_t)word,rgba);
    else if(format==3)ctr_rgb5a1_unpack((uint16_t)word,rgba);
    else ctr_rgba4_unpack((uint16_t)word,rgba);
}
static void write_pixel(uint8_t *p,unsigned format,const uint8_t c[4]) {
    if(format==0) { p[0]=(uint8_t)c[3];p[1]=(uint8_t)c[2];p[2]=(uint8_t)c[1];p[3]=(uint8_t)c[0];return; }
    if(format==1) { p[0]=(uint8_t)c[2];p[1]=(uint8_t)c[1];p[2]=(uint8_t)c[0];return; }
    unsigned word=format==2?ctr_rgb565_pack(c):
                  format==3?ctr_rgb5a1_pack(c):ctr_rgba4_pack(c);
    p[0]=(uint8_t)word;p[1]=(uint8_t)(word>>8);
}
static inline void rgba8_to_rgb565_bytes(const uint8_t *pixel,uint8_t *output) {
    output[0]=(uint8_t)((pixel[1]>>3)|((pixel[2]&0x1cu)<<3));
    output[1]=(uint8_t)((pixel[3]&0xf8u)|(pixel[2]>>5));
}
#if defined(__GNUC__) && defined(__BYTE_ORDER__)
static inline uint32_t aligned_rgb565(const uint8_t *pixel) {
    uint32_t abgr;
    memcpy(&abgr,__builtin_assume_aligned(pixel,4),sizeof(abgr));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((abgr&0xf8u)<<8)|((abgr>>5)&0x07e0u)|((abgr>>19)&0x1fu);
#else
    return ((abgr>>16)&0xf800u)|((abgr>>13)&0x07e0u)|((abgr>>11)&0x1fu);
#endif
}
static inline void rgba8_pair_to_rgb565(const uint8_t *pixels,uint8_t *output) {
    uint32_t pair=aligned_rgb565(pixels)|(aligned_rgb565(pixels+4u)<<16);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    pair=__builtin_bswap32(pair);
#endif
    memcpy(__builtin_assume_aligned(output,4),&pair,sizeof(pair));
}
#endif
static void tiled_rgba8_to_linear_rgb565(const ctr_transfer *t,const uint8_t *source,uint8_t *output) {
#if defined(__GNUC__) && defined(__BYTE_ORDER__)
    bool word_access=(((uintptr_t)source|(uintptr_t)output)&3u)==0u && !(t->width&1u);
#endif
    for(unsigned y=0;y<t->height;++y) {
        const uint8_t *src=source+4u*row(y,t->input_width,true);
        unsigned output_y=t->flip?t->height-1u-y:y;
        uint8_t *dst=output+2u*(size_t)output_y*t->width;
        unsigned tiles=t->width/8u;
        for(unsigned x=0;x<tiles;++x,dst+=16u) {
            const uint8_t *tile=src+256u*x;
#if defined(__GNUC__) && defined(__BYTE_ORDER__)
            if(word_access) {
                rgba8_pair_to_rgb565(tile+4u*morton(0),dst+0);
                rgba8_pair_to_rgb565(tile+4u*morton(2),dst+4);
                rgba8_pair_to_rgb565(tile+4u*morton(4),dst+8);
                rgba8_pair_to_rgb565(tile+4u*morton(6),dst+12);
                continue;
            }
#endif
            rgba8_to_rgb565_bytes(tile+4u*morton(0),dst+0);
            rgba8_to_rgb565_bytes(tile+4u*morton(1),dst+2);
            rgba8_to_rgb565_bytes(tile+4u*morton(2),dst+4);
            rgba8_to_rgb565_bytes(tile+4u*morton(3),dst+6);
            rgba8_to_rgb565_bytes(tile+4u*morton(4),dst+8);
            rgba8_to_rgb565_bytes(tile+4u*morton(5),dst+10);
            rgba8_to_rgb565_bytes(tile+4u*morton(6),dst+12);
            rgba8_to_rgb565_bytes(tile+4u*morton(7),dst+14);
        }
        for(unsigned lane=0;lane<t->width%8u;++lane)
            rgba8_to_rgb565_bytes(src+256u*tiles+4u*morton(lane),dst+2u*lane);
    }
}
bool ctr_transfer_convert(uint32_t in,uint32_t out,uint32_t flags,const uint8_t *source,size_t source_size,
                          uint8_t *output,size_t output_size) {
    ctr_transfer t;if(!ctr_transfer_init(&t,in,out,flags) || !source || !output ||
       source_size<t.input_bytes || output_size<t.output_bytes)return false;
    uintptr_t a=(uintptr_t)source,b=(uintptr_t)output;
    if(t.input_bytes>UINTPTR_MAX-a || t.output_bytes>UINTPTR_MAX-b || (a<b+t.output_bytes && b<a+t.input_bytes))return false;
    if(!t.input_linear && !t.output_tiled && t.input_format==0 && t.output_format==2) {
        tiled_rgba8_to_linear_rgb565(&t,source,output);
        return true;
    }
    unsigned input_bpp=bpp(t.input_format),output_bpp=bpp(t.output_format);
    bool same=t.input_format==t.output_format;
    for(unsigned y=0;y<t.height;++y) {
        unsigned output_y=t.flip?t.height-1-y:y;
        const uint8_t *src=source+row(y,t.input_width,!t.input_linear)*input_bpp;
        uint8_t *dst=output+row(output_y,t.width,t.output_tiled)*output_bpp;
        if(same && t.input_linear && !t.output_tiled) {
            memcpy(dst,src,(size_t)t.width*input_bpp);
            continue;
        }
        for(unsigned x=0;x<t.width;++x) {
            const uint8_t *in_pixel=src+column(x,!t.input_linear)*input_bpp;
            uint8_t *out_pixel=dst+column(x,t.output_tiled)*output_bpp;
            if(same)memcpy(out_pixel,in_pixel,input_bpp);
            else {
                uint8_t rgba[4];
                read_pixel(in_pixel,t.input_format,rgba);
                write_pixel(out_pixel,t.output_format,rgba);
            }
        }
    }
    return true;
}
