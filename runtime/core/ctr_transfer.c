/* Original display-transfer conversion from wire/layout facts. */
#include "ctr_transfer.h"
static unsigned bpp(unsigned format) { return format==0?4:format==1?3:2; }
static size_t pixel(unsigned x,unsigned y,unsigned width,bool tiled) {
    if(!tiled)return (size_t)y*width+x;
    unsigned small=0;
    for(unsigned i=0;i<3;++i)small|=((x>>i)&1)<<(i*2)|((y>>i)&1)<<(i*2+1);
    return ((size_t)(y/8)*(width/8)+x/8)*64+small;
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
bool ctr_transfer_convert(uint32_t in,uint32_t out,uint32_t flags,const uint8_t *source,size_t source_size,
                          uint8_t *output,size_t output_size) {
    ctr_transfer t;if(!ctr_transfer_init(&t,in,out,flags) || !source || !output ||
       source_size<t.input_bytes || output_size<t.output_bytes)return false;
    uintptr_t a=(uintptr_t)source,b=(uintptr_t)output;
    if(t.input_bytes>UINTPTR_MAX-a || t.output_bytes>UINTPTR_MAX-b || (a<b+t.output_bytes && b<a+t.input_bytes))return false;
    for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
        uint8_t rgba[4];read_pixel(source+pixel(x,y,t.input_width,!t.input_linear)*bpp(t.input_format),t.input_format,rgba);
        unsigned output_y=t.flip?t.height-1-y:y;
        write_pixel(output+pixel(x,output_y,t.width,t.output_tiled)*bpp(t.output_format),t.output_format,rgba);
    }
    return true;
}
