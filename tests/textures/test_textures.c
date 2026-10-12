#include "ctr_texture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
static uint8_t source[1024],out[2048];
static void pixel(unsigned x,unsigned y,unsigned width,const uint8_t expected[4]) {
    CHECK(memcmp(out+(y*width+x)*4,expected,4)==0);
}
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8)); }
int main(void) {
    const uint8_t input[][4]={{1,2,3,4},{1,2,3,0},{1,252,0,0},{0,248,0,0},{205,171,0,0},
        {22,33,0,0},{22,33,0,0},{64,0,0,0},{128,0,0,0},{171,0,0,0},{146,0,0,0},{146,0,0,0}};
    const uint8_t expected[][4]={{4,3,2,1},{3,2,1,255},{255,132,0,255},{255,0,0,255},{170,187,204,221},
        {33,33,33,22},{33,22,0,255},{64,64,64,255},{0,0,0,128},{170,170,170,187},{34,34,34,255},{0,0,0,34}};
    const unsigned bpp[]={32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    for(unsigned format=0;format<14;++format) {
        size_t size=99;CHECK(ctr_texture_size(format,16,32,&size)==CTR_TEXTURE_OK);
        CHECK(size==16*32*bpp[format]/8);
    }
    for(unsigned format=0;format<12;++format) {
        size_t size;CHECK(ctr_texture_size(format,8,8,&size)==CTR_TEXTURE_OK);
        unsigned stride=bpp[format]>=8?bpp[format]/8:1;
        for(size_t i=0;i<size;i+=stride)memcpy(source+i,input[format],stride);
        CHECK(ctr_texture_decode(source,size,format,8,8,out,sizeof(out))==CTR_TEXTURE_OK);
        pixel(0,0,8,expected[format]);
        if(format<10)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixel(x,y,8,expected[format]);
        else { const uint8_t second_l[]={153,153,153,255},second_a[]={0,0,0,153};pixel(1,0,8,format==10?second_l:second_a); }
    }
    /* Asymmetric values expose Morton and tile-grid placement errors. */
    for(unsigned i=0;i<256;++i)source[i]=(uint8_t)i;
    CHECK(ctr_texture_decode(source,256,7,16,16,out,sizeof(out))==CTR_TEXTURE_OK);
    const unsigned rows[]={0,1,4,5,16,17,20,21},cols[]={0,2,8,10,32,34,40,42};
    for(unsigned tile=0;tile<4;++tile)for(unsigned i=0;i<8;++i) {
        uint8_t a[]={(uint8_t)(tile*64+rows[i]),(uint8_t)(tile*64+rows[i]),(uint8_t)(tile*64+rows[i]),255};
        uint8_t b[]={(uint8_t)(tile*64+cols[i]),(uint8_t)(tile*64+cols[i]),(uint8_t)(tile*64+cols[i]),255};
        pixel((tile%2)*8+i,(tile/2)*8,16,a);pixel((tile%2)*8,(tile/2)*8+i,16,b);
    }
    for(unsigned format=12;format<14;++format) {
        unsigned stride=format==12?8:16;
        memset(source,0,sizeof(source));
        for(unsigned block=0;block<4;++block) {
            uint8_t *p=source+block*stride;
            if(format==13) { for(unsigned i=0;i<8;++i)p[i]=(uint8_t)(i*34+16);p+=8; }
            put32(p,0);put32(p+4,0x123456e0);
        }
        CHECK(ctr_texture_decode(source,stride*4,format,8,8,out,sizeof(out))==CTR_TEXTURE_OK);
        const uint8_t left[]={64,98,132,format==12?255:0},right[]={36,70,104,format==12?255:204};
        pixel(0,0,8,left);pixel(3,0,8,right);
        if(format==13) { const uint8_t last[]={36,70,104,255};pixel(3,3,8,last); }
        /* Flip partitions horizontally without changing column-major selectors. */
        for(unsigned block=0;block<4;++block)put32(source+block*stride+(format==13?12:4),0x123456e1);
        CHECK(ctr_texture_decode(source,stride*4,format,8,8,out,sizeof(out))==CTR_TEXTURE_OK);
        const uint8_t flipped[]={64,98,132,format==12?255:204};pixel(3,0,8,flipped);
        /* Reject an invalid differential base in the LAST block before any writes. */
        put32(source+3*stride+(format==13?12:4),0xf9000002);
        memset(out,0xa5,sizeof(out));
        CHECK(ctr_texture_decode(source,stride*4,format,8,8,out,sizeof(out))==CTR_TEXTURE_COMPRESSED);
        for(size_t i=0;i<sizeof(out);++i)CHECK(out[i]==0xa5);
    }
    memset(out,0xa5,sizeof(out));size_t size=99;
    CHECK(ctr_texture_size(14,8,8,&size)==CTR_TEXTURE_FORMAT && size==99);
    for(unsigned dim=0;dim<=1032;++dim)if(dim<8 || dim>1024 || (dim&(dim-1)))
        CHECK(ctr_texture_size(0,dim,8,&size)==CTR_TEXTURE_DIMENSIONS);
    CHECK(ctr_texture_decode(NULL,256,0,8,8,out,sizeof(out))==CTR_TEXTURE_SOURCE);
    CHECK(ctr_texture_decode(source,255,0,8,8,out,sizeof(out))==CTR_TEXTURE_SOURCE);
    CHECK(ctr_texture_decode(source,256,0,8,8,NULL,256)==CTR_TEXTURE_DESTINATION);
    CHECK(ctr_texture_decode(source,256,0,8,8,out,255)==CTR_TEXTURE_DESTINATION);
    CHECK(ctr_texture_decode(source,256,0,8,8,source+4,256)==CTR_TEXTURE_ALIAS);
    for(size_t i=0;i<sizeof(out);++i)CHECK(out[i]==0xa5);
    printf("texture PASS %u checks\n",checks);return 0;
}
