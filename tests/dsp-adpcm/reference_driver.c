/* Original fixed-format fixture reader, not an application media/container parser. */
#include "ctr_dsp_adpcm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned get16(FILE *f) {
    int a=fgetc(f),b=fgetc(f);if(a==EOF || b==EOF)exit(2);
    return (unsigned)a|((unsigned)b<<8);
}
static uint32_t get32(FILE *f) { uint32_t a=get16(f),b=get16(f);return a|(b<<16); }
static int16_t signed16(unsigned bits) { return (int16_t)(bits<32768?(int32_t)bits:(int32_t)bits-65536); }
static void put16(FILE *f,int16_t value) { uint16_t bits=(uint16_t)value;fputc(bits&255,f);fputc(bits>>8,f); }
static void put32(FILE *f,uint32_t value) { put16(f,signed16(value&65535));put16(f,signed16(value>>16)); }
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");if(!in || !out)return 2;
    char magic[4];if(fread(magic,1,4,in)!=4 || memcmp(magic,"CAD1",4))return 2;
    uint32_t cases=get32(in);if(cases>10000)return 2;put32(out,cases);
    for(uint32_t test=0;test<cases;++test) {
        uint32_t bytes=get32(in),first=get32(in),count=get32(in);
        if(bytes>32 || !count || count>56 || first>55 || count>56-first)return 2;
        int16_t coefficients[16],whole[56],streamed[56],seeked[56];uint8_t encoded[32];
        for(unsigned i=0;i<16;++i)coefficients[i]=signed16(get16(in));
        int16_t h1=signed16(get16(in)),h2=signed16(get16(in));
        CtrDspAdpcmState initial={first,h1,h2},state=initial,parts=initial;
        if(fread(encoded,1,bytes,in)!=bytes)return 2;
        if(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&state,whole,56,count)!=CTR_DSP_ADPCM_OK)return 1;
        size_t done=0;
        while(done<count) {
            size_t take=1+(done%7);if(take>count-done)take=count-done;
            if(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&parts,streamed+done,56-done,take)!=CTR_DSP_ADPCM_OK)return 1;
            done+=take;
        }
        if(memcmp(whole,streamed,count*sizeof(*whole)) || state.sample!=parts.sample ||
           state.history1!=parts.history1 || state.history2!=parts.history2)return 1;
        size_t split=count/2;CtrDspAdpcmState seek={first+split,split?whole[split-1]:initial.history1,
            split>1?whole[split-2]:(split?initial.history1:initial.history2)};
        if(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&seek,seeked,56,count-split)!=CTR_DSP_ADPCM_OK ||
           memcmp(whole+split,seeked,(count-split)*sizeof(*whole)) || seek.sample!=state.sample ||
           seek.history1!=state.history1 || seek.history2!=state.history2)return 1;
        put32(out,count);put32(out,(uint32_t)state.sample);put16(out,state.history1);put16(out,state.history2);
        for(unsigned i=0;i<count;++i)put16(out,whole[i]);
    }
    if(fgetc(in)!=EOF || ferror(in) || fclose(in) || ferror(out) || fclose(out))return 2;
    return 0;
}
