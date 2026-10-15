#include "ctr_dsp_adpcm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static bool same(CtrDspAdpcmState a,CtrDspAdpcmState b) {
    return a.sample==b.sample && a.history1==b.history1 && a.history2==b.history2;
}
static void golden(void) {
    const uint8_t encoded[]={0x00,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0x00,0xef};
    const int16_t expected[]={0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1};
    int16_t coefficients[16]={0},output[16];CtrDspAdpcmState state={0,123,-456};
    CHECK(ctr_dsp_adpcm_decode(encoded,sizeof(encoded),coefficients,&state,output,16,16)==CTR_DSP_ADPCM_OK);
    CHECK(!memcmp(output,expected,sizeof(expected)));CHECK(state.sample==16 && state.history1==-1 && state.history2==-2);
    int16_t stream[16];CtrDspAdpcmState split={0,123,-456};
    for(unsigned i=0;i<16;++i)CHECK(ctr_dsp_adpcm_decode(encoded,sizeof(encoded),coefficients,&split,stream+i,1,1)==CTR_DSP_ADPCM_OK);
    CHECK(!memcmp(stream,expected,sizeof(expected)));CHECK(same(state,split));
    CtrDspAdpcmState seek={13,-4,-5};
    CHECK(ctr_dsp_adpcm_decode(encoded,sizeof(encoded),coefficients,&seek,stream,16,3)==CTR_DSP_ADPCM_OK);
    CHECK(!memcmp(stream,expected+13,3*sizeof(*stream)));CHECK(same(seek,state));
    /* Rounding is floor(x+1/2), including negative ties; no signed shifts. */
    const uint8_t zero[]={0,0};coefficients[0]=1024;
    for(int h=-3;h<=3;++h) {
        state=(CtrDspAdpcmState){0,(int16_t)h,0};
        CHECK(ctr_dsp_adpcm_decode(zero,2,coefficients,&state,output,16,1)==CTR_DSP_ADPCM_OK);
        CHECK(output[0]==(h>=0?(h+1)/2:h/2));
    }
    /* Scale 15 is valid; clip before updating the prediction history. */
    const uint8_t clipped[]={0x7f,0x70,0x80};coefficients[14]=2048;
    state=(CtrDspAdpcmState){0,0,0};
    CHECK(ctr_dsp_adpcm_decode(clipped,3,coefficients,&state,output,16,4)==CTR_DSP_ADPCM_OK);
    const int16_t saturation[]={32767,32767,-32768,-32768};CHECK(!memcmp(output,saturation,sizeof(saturation)));
    CHECK(state.history1==-32768 && state.history2==-32768);
}
static void failures(void) {
    uint8_t encoded[16]={0};int16_t coefficients[16]={0},output[32],before[32];
    for(unsigned i=0;i<32;++i)output[i]=before[i]=(int16_t)(i+1000);
    CtrDspAdpcmState state={0,123,-456},initial=state;
#define REJECT(call,reason) do { CHECK((call)==(reason));CHECK(same(state,initial));CHECK(!memcmp(output,before,sizeof(output))); } while(0)
    encoded[8]=0x80;
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,output,32,15),CTR_DSP_ADPCM_PREDICTOR);
    encoded[8]=0;
    REJECT(ctr_dsp_adpcm_decode(encoded,9,coefficients,&state,output,32,15),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,output,14,15),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,output,32,CTR_DSP_ADPCM_MAX_SAMPLES+1u),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode(NULL,16,coefficients,&state,output,32,1),CTR_DSP_ADPCM_ARGUMENT);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,NULL,&state,output,32,1),CTR_DSP_ADPCM_ARGUMENT);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,NULL,output,32,1),CTR_DSP_ADPCM_ARGUMENT);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,NULL,32,1),CTR_DSP_ADPCM_ARGUMENT);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,(int16_t *)encoded,32,1),CTR_DSP_ADPCM_OVERLAP);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,coefficients,32,1),CTR_DSP_ADPCM_OVERLAP);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,(int16_t *)&state,32,1),CTR_DSP_ADPCM_OVERLAP);
    REJECT(ctr_dsp_adpcm_decode((uint8_t *)&state,sizeof(state),coefficients,&state,output,32,1),CTR_DSP_ADPCM_OVERLAP);
    union { CtrDspAdpcmState state;int16_t coefficients[16]; } aliased={0};aliased.state=state;
    REJECT(ctr_dsp_adpcm_decode(encoded,16,aliased.coefficients,&aliased.state,output,32,1),CTR_DSP_ADPCM_OVERLAP);
    CHECK(same(aliased.state,state));
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,(int16_t *)(UINTPTR_MAX-1),32,1),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode((uint8_t *)(UINTPTR_MAX-1),16,coefficients,&state,output,32,1),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,(int16_t *)(UINTPTR_MAX-1),&state,output,32,1),CTR_DSP_ADPCM_BOUNDS);
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,(CtrDspAdpcmState *)(UINTPTR_MAX-1),output,32,1),CTR_DSP_ADPCM_BOUNDS);
    state.sample=SIZE_MAX;initial=state;
    REJECT(ctr_dsp_adpcm_decode(encoded,16,coefficients,&state,output,32,1),CTR_DSP_ADPCM_BOUNDS);
    CHECK(ctr_dsp_adpcm_decode(NULL,0,coefficients,&state,NULL,0,0)==CTR_DSP_ADPCM_OK);CHECK(same(state,initial));
    state.sample=14;initial=state;
    /* Skip invalid earlier frames only when caller supplies an explicit checkpoint. */
    encoded[0]=0xf0;
    CHECK(ctr_dsp_adpcm_decode(encoded,10,coefficients,&state,output,32,1)==CTR_DSP_ADPCM_OK);
    CHECK(state.sample==15 && state.history1==0 && state.history2==123);
    CHECK(!strcmp(ctr_dsp_adpcm_result_name(CTR_DSP_ADPCM_PREDICTOR),"predictor"));
    CHECK(!strcmp(ctr_dsp_adpcm_result_name((CtrDspAdpcmResult)99),"invalid"));
#undef REJECT
}
static void sample_limit(void) {
    size_t samples=CTR_DSP_ADPCM_MAX_SAMPLES,bytes=((samples+13)/14)*8;
    uint8_t *encoded=calloc(bytes,1);int16_t *output=malloc((samples+1)*sizeof(*output));
    int16_t coefficients[16]={0};CtrDspAdpcmState state={0,0,0};CHECK(encoded && output);
    output[samples]=12345;
    CHECK(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&state,output,samples+1,samples+1)==CTR_DSP_ADPCM_BOUNDS);
    CHECK(state.sample==0 && output[samples]==12345);
    CHECK(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&state,output,samples+1,samples)==CTR_DSP_ADPCM_OK);
    bool silent=true;for(size_t i=0;i<samples;++i)if(output[i])silent=false;
    CHECK(silent && output[samples]==12345);CHECK(state.sample==samples && !state.history1 && !state.history2);
    free(encoded);free(output);
}
int main(void) {
    golden();failures();sample_limit();printf("{\"ok\":true,\"assertions\":%u,\"dsp_adpcm_reference\":true,\"console_verified\":false}\n",checks);return 0;
}
