/* Original display-transfer conversion from wire/layout facts. */
#include "ctr_transfer.h"
static unsigned bpp(unsigned format) { return format==0?4:format==1?3:2; }
static unsigned expand(unsigned n,unsigned bits) { return bits==4?n*17:bits==5?(n<<3)|(n>>2):(n<<2)|(n>>4); }
static size_t pixel(unsigned x,unsigned y,unsigned width,bool tiled) {
    if(!tiled)return (size_t)y*width+x;
    unsigned small=0;
    for(unsigned i=0;i<3;++i)small|=((x>>i)&1)<<(i*2)|((y>>i)&1)<<(i*2+1);
    return ((size_t)(y/8)*(width/8)+x/8)*64+small;
}
bool ctr_transfer_init(ctr_transfer *t,uint32_t in,uint32_t out,uint32_t flags) {
    unsigned width=in&65535,height=in>>16,input=(flags>>8)&7,output=(flags>>12)&7;
    if(!t || in!=out || !width || !height || width>1024 || height>1024 || input>4 || output>4 ||
       (flags&~UINT32_C(0x00007723)))return false;
    bool linear=(flags&2)!=0,tiled=linear!=((flags&32)!=0);
    if((!linear || tiled) && ((width&7) || (height&7)))return false;
    *t=(ctr_transfer){width,height,input,output,linear,tiled,(flags&1)!=0,
                      (size_t)width*height*bpp(input),(size_t)width*height*bpp(output)};
    return true;
}
static void read_pixel(const uint8_t *p,unsigned format,unsigned rgba[4]) {
    unsigned word=(unsigned)p[0]|(unsigned)p[1]<<8;rgba[3]=255;
    if(format==0) { rgba[0]=p[3];rgba[1]=p[2];rgba[2]=p[1];rgba[3]=p[0]; }
    else if(format==1) { rgba[0]=p[2];rgba[1]=p[1];rgba[2]=p[0]; }
    else if(format==2) { rgba[0]=expand(word>>11,5);rgba[1]=expand((word>>5)&63,6);rgba[2]=expand(word&31,5); }
    else if(format==3) { rgba[0]=expand(word>>11,5);rgba[1]=expand((word>>6)&31,5);rgba[2]=expand((word>>1)&31,5);rgba[3]=(word&1)?255:0; }
    else { rgba[0]=expand(word>>12,4);rgba[1]=expand((word>>8)&15,4);rgba[2]=expand((word>>4)&15,4);rgba[3]=expand(word&15,4); }
}
static void write_pixel(uint8_t *p,unsigned format,const unsigned c[4]) {
    if(format==0) { p[0]=(uint8_t)c[3];p[1]=(uint8_t)c[2];p[2]=(uint8_t)c[1];p[3]=(uint8_t)c[0];return; }
    if(format==1) { p[0]=(uint8_t)c[2];p[1]=(uint8_t)c[1];p[2]=(uint8_t)c[0];return; }
    unsigned word=format==2?((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3):
                  format==3?((c[0]>>3)<<11)|((c[1]>>3)<<6)|((c[2]>>3)<<1)|(c[3]>>7):
                  ((c[0]>>4)<<12)|((c[1]>>4)<<8)|((c[2]>>4)<<4)|(c[3]>>4);
    p[0]=(uint8_t)word;p[1]=(uint8_t)(word>>8);
}
bool ctr_transfer_convert(uint32_t in,uint32_t out,uint32_t flags,const uint8_t *source,size_t source_size,
                          uint8_t *output,size_t output_size) {
    ctr_transfer t;if(!ctr_transfer_init(&t,in,out,flags) || !source || !output ||
       source_size<t.input_bytes || output_size<t.output_bytes)return false;
    uintptr_t a=(uintptr_t)source,b=(uintptr_t)output;
    if(t.input_bytes>UINTPTR_MAX-a || t.output_bytes>UINTPTR_MAX-b || (a<b+t.output_bytes && b<a+t.input_bytes))return false;
    for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x) {
        unsigned rgba[4];read_pixel(source+pixel(x,y,t.width,!t.input_linear)*bpp(t.input_format),t.input_format,rgba);
        unsigned output_y=t.flip?t.height-1-y:y;
        write_pixel(output+pixel(x,output_y,t.width,t.output_tiled)*bpp(t.output_format),t.output_format,rgba);
    }
    return true;
}
