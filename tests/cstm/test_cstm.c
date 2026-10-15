#include "ctr_cstm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void w16(uint8_t *d,size_t at,unsigned v) { d[at]=(uint8_t)v;d[at+1]=(uint8_t)(v>>8); }
static void w32(uint8_t *d,size_t at,uint32_t v) { w16(d,at,v);w16(d,at+2,v>>16); }
static void reference(uint8_t *d,size_t at,unsigned type,uint32_t offset) { w16(d,at,type);w32(d,at+4,offset); }
/* Original stereo, two blocks, 14+3 samples/channel. Final channel stride is
 * four bytes but only three are payload. INFO stream is deliberately relocated. */
static void fixture(uint8_t d[512]) {
    memset(d,0,512);memcpy(d,"CSTM",4);w16(d,4,0xfeff);w16(d,6,64);w32(d,8,0x02000000);w32(d,12,512);w16(d,16,3);
    reference(d,20,0x4002,448);w32(d,28,64);
    reference(d,32,0x4000,64);w32(d,40,352);
    reference(d,44,0x4001,416);w32(d,52,32);
    memcpy(d+64,"INFO",4);w32(d,68,352);
    reference(d,72,0x4100,56);reference(d,80,0,UINT32_MAX);reference(d,88,0x0101,120);
    size_t s=128;d[s]=2;d[s+2]=2;w32(d,s+4,32000);w32(d,s+12,17);w32(d,s+16,2);
    w32(d,s+20,8);w32(d,s+24,14);w32(d,s+28,3);w32(d,s+32,3);w32(d,s+36,4);
    w32(d,s+40,4);w32(d,s+44,14);reference(d,s+48,0x1f00,24);
    w32(d,192,2);reference(d,196,0x4102,32);reference(d,204,0x4102,40);
    reference(d,224,0x0300,32);reference(d,232,0x0300,80);
    /* Predictor h1 (2048,0). Every nibble adds one on left, subtracts on right. */
    w16(d,256,2048);w16(d,312,2048);
    w16(d,290,10);w16(d,292,9);w16(d,346,-10);w16(d,348,-9);
    memcpy(d+416,"SEEK",4);w32(d,420,32);
    w16(d,424,10);w16(d,426,9);w16(d,428,-10);w16(d,430,-9);
    w16(d,432,24);w16(d,434,23);w16(d,436,-24);w16(d,438,-23);
    memcpy(d+448,"DATA",4);w32(d,452,64);
    memset(d+481,0x11,7);memset(d+489,0xff,7);
    d[497]=0x11;d[498]=0x1a;d[499]=0x80; /* poison unused nibble/padding */
    d[501]=0xff;d[502]=0xfa;d[503]=0x80;
}
static void failure(uint8_t *d,size_t n) {
    CtrCstmInfo info,before;memset(&info,0x5a,sizeof(info));memcpy(&before,&info,sizeof(info));
    CHECK(ctr_cstm_info(d,n,&info)!=CTR_CSTM_OK);CHECK(!memcmp(&info,&before,sizeof(info)));
    CtrCstmBlock block,prior;memset(&block,0x6b,sizeof(block));memcpy(&prior,&block,sizeof(block));
    CHECK(ctr_cstm_block(d,n,1,1,&block)!=CTR_CSTM_OK);CHECK(!memcmp(&block,&prior,sizeof(block)));
}
static void valid(void) {
    uint8_t d[512];fixture(d);CtrCstmInfo info;
    CHECK(ctr_cstm_info(d,sizeof(d),&info)==CTR_CSTM_OK);
    CHECK(info.samples==17 && info.channels==2 && info.sample_rate==32000 && info.blocks==2);
    for(unsigned c=0;c<2;++c) {
        CtrDspAdpcmState sequential={0,c?-10:10,c?-9:9};int16_t pcm[17];
        for(unsigned b=0;b<2;++b) {
            CtrCstmBlock block;CHECK(ctr_cstm_block(d,512,b,c,&block)==CTR_CSTM_OK);
            CHECK(block.first_sample==b*14 && block.samples==(b?3:14) && block.bytes==(b?3:8));
            CHECK(block.encoded==d+(b?496+c*4:480+c*8));
            CHECK(block.state.sample==0 && block.state.history1==sequential.history1 && block.state.history2==sequential.history2);
            CHECK(block.coefficients[0]==2048);
            CtrDspAdpcmState state=block.state;
            CHECK(ctr_dsp_adpcm_decode(block.encoded,block.bytes,block.coefficients,&state,pcm+b*14,17-b*14,block.samples)==CTR_DSP_ADPCM_OK);
            sequential=state;
            for(unsigned i=0;i<block.samples;++i)CHECK(pcm[b*14+i]==(c?-(int)(11+b*14+i):(int)(11+b*14+i)));
        }
    }
    CtrCstmBlock block,before;memset(&block,0xa5,sizeof(block));memcpy(&before,&block,sizeof(block));
    CHECK(ctr_cstm_block(d,512,2,0,&block)==CTR_CSTM_BOUNDS);CHECK(!memcmp(&block,&before,sizeof(block)));
    CHECK(ctr_cstm_block(d,512,0,2,&block)==CTR_CSTM_BOUNDS);CHECK(!memcmp(&block,&before,sizeof(block)));
    /* An encoded predictor fault belongs to the codec and preserves PCM/state. */
    d[496]=0x80;CHECK(ctr_cstm_block(d,512,1,0,&block)==CTR_CSTM_OK);
    int16_t pcm[3]={123,456,789};CtrDspAdpcmState state=block.state;
    CHECK(ctr_dsp_adpcm_decode(block.encoded,block.bytes,block.coefficients,&state,pcm,3,3)==CTR_DSP_ADPCM_PREDICTOR);
    CHECK(pcm[0]==123 && pcm[1]==456 && pcm[2]==789 && state.sample==0);
}
static void malformed(void) {
    uint8_t d[512];for(size_t n=0;n<512;++n) {
        fixture(d);failure(d,n);
        /* Also bypass the top-level declared-length check to reach section bounds. */
        w32(d,12,(uint32_t)n);failure(d,n);
    }
    const struct { size_t at;uint32_t value; } edits[]={
        {8,0x02010000},{12,513},{24,480},{28,63},{36,UINT32_MAX},{40,351},{48,64},
        {68,351},{76,UINT32_MAX},{92,UINT32_MAX},{132,0},{136,1},{140,16},
        {144,0},{144,UINT32_MAX},{148,9},{152,13},{152,UINT32_MAX},
        {156,2},{156,9},{160,0},{160,15},{164,2},{164,9},{168,8},{172,7},
        {180,UINT32_MAX},{192,1},{200,UINT32_MAX},{208,UINT32_MAX},{228,UINT32_MAX},
        {236,UINT32_MAX},{420,7},{452,63}};
    for(size_t i=0;i<sizeof(edits)/sizeof(*edits);++i) { fixture(d);w32(d,edits[i].at,edits[i].value);failure(d,512); }
    const struct { size_t at;unsigned value; } bytes[]={
        {0,'X'},{4,0xfe},{128,1},{129,1},{130,0},{130,3},{288,0x80},{424,11},
        {448,'X'},{196,0},{224,1},{312+32,0x80}};
    for(size_t i=0;i<sizeof(bytes)/sizeof(*bytes);++i) { fixture(d);d[bytes[i].at]=(uint8_t)bytes[i].value;failure(d,512); }
    fixture(d);CtrCstmInfo info;CtrCstmBlock block;
    CHECK(ctr_cstm_info(NULL,512,&info)==CTR_CSTM_ARGUMENT);
    CHECK(ctr_cstm_info(d,512,NULL)==CTR_CSTM_ARGUMENT);
    CHECK(ctr_cstm_block(d,512,0,0,NULL)==CTR_CSTM_ARGUMENT);
    CHECK(ctr_cstm_info(d,CTR_CSTM_MAX_BYTES+1u,&info)==CTR_CSTM_BOUNDS);
    CHECK(ctr_cstm_info((const uint8_t *)(UINTPTR_MAX-1),512,&info)==CTR_CSTM_BOUNDS);
    CHECK(ctr_cstm_info(d,512,(CtrCstmInfo *)d)==CTR_CSTM_OVERLAP);
    CHECK(ctr_cstm_block(d,512,0,0,(CtrCstmBlock *)d)==CTR_CSTM_OVERLAP);
    CHECK(ctr_cstm_block(d,512,0,0,(CtrCstmBlock *)(UINTPTR_MAX-1))==CTR_CSTM_BOUNDS);
    /* Deterministic mutations may be accepted (padding/coefficients can change).
     * Exercise both edge blocks whenever the structural subset still validates. */
    uint32_t random=0x4353544d;
    for(unsigned i=0;i<8192;++i) {
        fixture(d);random=random*1664525u+1013904223u;
        d[random%512]^=(uint8_t)(1u<<((random>>16)%8));
        if(ctr_cstm_info(d,512,&info)==CTR_CSTM_OK) {
            for(unsigned c=0;c<info.channels;++c) {
                CHECK(ctr_cstm_block(d,512,0,c,&block)==CTR_CSTM_OK);
                CHECK(ctr_cstm_block(d,512,info.blocks-1,c,&block)==CTR_CSTM_OK);
                CHECK(block.encoded>=d && block.encoded+block.bytes<=d+512);
            }
        } else failure(d,512);
    }
    for(int i=0;i<=CTR_CSTM_OVERLAP;++i)CHECK(strcmp(ctr_cstm_result_name((CtrCstmResult)i),"invalid"));
    CHECK(!strcmp(ctr_cstm_result_name((CtrCstmResult)99),"invalid"));
}
int main(void) { valid();malformed();printf("CSTM: %u checks passed\n",checks);return 0; }
