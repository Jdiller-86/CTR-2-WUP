#include "ctr_dsp_adpcm.h"
#include <stdbool.h>
#include <limits.h>

static bool bounded(const void *p,size_t bytes) {
    return bytes<=UINTPTR_MAX-(uintptr_t)p;
}
static bool separate(const void *a,size_t an,const void *b,size_t bn) {
    return !an || !bn || (uintptr_t)a+an<=(uintptr_t)b || (uintptr_t)b+bn<=(uintptr_t)a;
}
CtrDspAdpcmResult ctr_dsp_adpcm_decode(const uint8_t *encoded,size_t bytes,
    const int16_t coefficients[16],CtrDspAdpcmState *state,
    int16_t *output,size_t capacity,size_t samples) {
    if(!state || !coefficients || (bytes && !encoded) || (samples && !output))return CTR_DSP_ADPCM_ARGUMENT;
    if(samples>CTR_DSP_ADPCM_MAX_SAMPLES || samples>capacity ||
       !bounded(encoded,bytes) || !bounded(output,samples*sizeof(*output)) ||
       !bounded(coefficients,16*sizeof(*coefficients)) || !bounded(state,sizeof(*state)))return CTR_DSP_ADPCM_BOUNDS;
    size_t written=samples*sizeof(*output);
    if(!separate(output,written,encoded,bytes) || !separate(output,written,coefficients,16*sizeof(*coefficients)) ||
       !separate(output,written,state,sizeof(*state)) || !separate(state,sizeof(*state),encoded,bytes) ||
       !separate(state,sizeof(*state),coefficients,16*sizeof(*coefficients)))return CTR_DSP_ADPCM_OVERLAP;
    if(state->sample>SIZE_MAX-samples)return CTR_DSP_ADPCM_BOUNDS;
    if(!samples)return CTR_DSP_ADPCM_OK;
    size_t last=state->sample+samples-1;
    if(last/14*8+1+last%14/2>=bytes)return CTR_DSP_ADPCM_BOUNDS;
    /* Validate every requested frame before any output or history is published. */
    for(size_t frame=state->sample/14;frame<=last/14;++frame)
        if((encoded[frame*8]>>4)>7)return CTR_DSP_ADPCM_PREDICTOR;
    int16_t h1=state->history1,h2=state->history2;
    for(size_t i=0;i<samples;++i) {
        size_t sample=state->sample+i,frame=sample/14*8,within=sample%14;
        unsigned header=encoded[frame],predictor=header>>4;
        int nibble=(encoded[frame+1+within/2]>>(within%2?0:4))&15;
        if(nibble>=8)nibble-=16;
        int64_t sum=(int64_t)nibble*(INT64_C(1)<<(header&15))*2048+
            (int64_t)coefficients[predictor*2]*h1+(int64_t)coefficients[predictor*2+1]*h2+1024;
        /* Explicit floor division avoids implementation-defined negative shifts. */
        int64_t value=sum>=0?sum/2048:-((-sum+2047)/2048);
        if(value>INT16_MAX)value=INT16_MAX;
        if(value<INT16_MIN)value=INT16_MIN;
        output[i]=(int16_t)value;h2=h1;h1=(int16_t)value;
    }
    state->sample+=samples;state->history1=h1;state->history2=h2;
    return CTR_DSP_ADPCM_OK;
}
const char *ctr_dsp_adpcm_result_name(CtrDspAdpcmResult result) {
    switch(result) {
    case CTR_DSP_ADPCM_OK:return "ok";
    case CTR_DSP_ADPCM_ARGUMENT:return "argument";
    case CTR_DSP_ADPCM_BOUNDS:return "bounds";
    case CTR_DSP_ADPCM_PREDICTOR:return "predictor";
    case CTR_DSP_ADPCM_OVERLAP:return "overlap";
    default:return "invalid";
    }
}
