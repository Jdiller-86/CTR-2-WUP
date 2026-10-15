#include "ctr_cstm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t get32(FILE *f) {
    uint32_t v=0;for(unsigned i=0;i<4;++i) { int c=fgetc(f);if(c==EOF)exit(2);v|=(uint32_t)c<<(8*i); }return v;
}
static void put32(FILE *f,uint32_t v) { for(unsigned i=0;i<4;++i)fputc((int)((v>>(i*8))&255),f); }
static void put16(FILE *f,int16_t v) { unsigned n=(uint16_t)v;fputc((int)(n&255),f);fputc((int)(n>>8),f); }
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");if(!in || !out)return 2;
    char magic[4];if(fread(magic,1,4,in)!=4 || memcmp(magic,"CTM1",4))return 2;
    uint32_t cases=get32(in);if(cases>1024)return 2;put32(out,cases);
    for(uint32_t i=0;i<cases;++i) {
        uint32_t bytes=get32(in);if(bytes>65536)return 2;
        uint8_t *data=malloc(bytes);if(!data || fread(data,1,bytes,in)!=bytes)return 2;
        CtrCstmInfo info;CtrCstmResult result=ctr_cstm_info(data,bytes,&info);
        if(result!=CTR_CSTM_OK || info.block_samples>1024) {
            fprintf(stderr,"case %u metadata: %s\n",i,ctr_cstm_result_name(result));return 1;
        }
        put32(out,info.samples);put32(out,info.channels);put32(out,info.sample_rate);put32(out,info.blocks);
        for(unsigned c=0;c<info.channels;++c) {
            CtrDspAdpcmState sequential={0};
            for(uint32_t b=0;b<info.blocks;++b) {
                CtrCstmBlock block;if(ctr_cstm_block(data,bytes,b,c,&block)!=CTR_CSTM_OK)return 1;
                if(block.first_sample!=b*info.block_samples || block.state.sample!=0)return 1;
                if(!b)sequential=block.state;
                if(block.state.history1!=sequential.history1 || block.state.history2!=sequential.history2)return 1;
                CtrDspAdpcmState seek=block.state;sequential.sample=0;
                int16_t checkpoint[1024],continuous[1024];
                if(ctr_dsp_adpcm_decode(block.encoded,block.bytes,block.coefficients,&seek,checkpoint,1024,block.samples)!=CTR_DSP_ADPCM_OK)return 1;
                /* Continue the prior block's decoded histories, in small chunks. */
                for(size_t n=0;n<block.samples;) {
                    size_t take=block.samples-n;if(take>5)take=5;
                    if(ctr_dsp_adpcm_decode(block.encoded,block.bytes,block.coefficients,&sequential,continuous+n,1024-n,take)!=CTR_DSP_ADPCM_OK)return 1;
                    n+=take;
                }
                if(memcmp(checkpoint,continuous,block.samples*sizeof(*checkpoint)) ||
                   seek.history1!=sequential.history1 || seek.history2!=sequential.history2)return 1;
                put32(out,block.first_sample);put32(out,block.samples);
                put16(out,seek.history1);put16(out,seek.history2);
                for(uint32_t n=0;n<block.samples;++n)put16(out,checkpoint[n]);
            }
        }
        free(data);
    }
    if(fgetc(in)!=EOF || ferror(in) || fclose(in) || ferror(out) || fclose(out))return 2;
    return 0;
}
