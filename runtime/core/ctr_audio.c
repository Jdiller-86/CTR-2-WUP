#include "ctr_audio.h"
#include <string.h>

#define AUDIO_GUARD UINT32_C(0x41554431)
typedef struct {
    bool loaded;
    uint32_t block;
    CtrCstmBlock channels[2];
} BlockCache;
static bool separate(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    return a+an<=b || b+bn<=a;
}
static bool span(const void *p,size_t n) { return n<=UINTPTR_MAX-(uintptr_t)p; }
static void count(uint64_t *v,uint64_t n) { *v=UINT64_MAX-*v<n?UINT64_MAX:*v+n; }
static CtrAudioResult cstm_result(CtrCstmResult r) {
    switch(r) {
    case CTR_CSTM_OK:return CTR_AUDIO_OK;
    case CTR_CSTM_ARGUMENT:return CTR_AUDIO_ARGUMENT;
    case CTR_CSTM_BOUNDS:return CTR_AUDIO_BOUNDS;
    case CTR_CSTM_FORMAT:return CTR_AUDIO_FORMAT;
    case CTR_CSTM_UNSUPPORTED:return CTR_AUDIO_UNSUPPORTED;
    case CTR_CSTM_OVERLAP:return CTR_AUDIO_OVERLAP;
    default:return CTR_AUDIO_STATE;
    }
}
static CtrAudioResult codec_result(CtrDspAdpcmResult r) {
    switch(r) {
    case CTR_DSP_ADPCM_OK:return CTR_AUDIO_OK;
    case CTR_DSP_ADPCM_ARGUMENT:return CTR_AUDIO_ARGUMENT;
    case CTR_DSP_ADPCM_BOUNDS:return CTR_AUDIO_BOUNDS;
    case CTR_DSP_ADPCM_PREDICTOR:return CTR_AUDIO_PREDICTOR;
    case CTR_DSP_ADPCM_OVERLAP:return CTR_AUDIO_OVERLAP;
    default:return CTR_AUDIO_STATE;
    }
}
static bool same_info(const CtrCstmInfo *a,const CtrCstmInfo *b) {
    return a->sample_rate==b->sample_rate && a->samples==b->samples &&
        a->blocks==b->blocks && a->block_samples==b->block_samples &&
        a->last_samples==b->last_samples && a->block_bytes==b->block_bytes &&
        a->last_bytes==b->last_bytes && a->last_padded_bytes==b->last_padded_bytes &&
        a->channels==b->channels;
}
static CtrAudioResult engine_argument(const CtrAudioEngine *e) {
    if(!e)return CTR_AUDIO_ARGUMENT;
    if(!span(e,sizeof(*e)))return CTR_AUDIO_BOUNDS;
    if(e->guard!=AUDIO_GUARD)return CTR_AUDIO_STATE;
    return CTR_AUDIO_OK;
}
static CtrAudioResult validate(const CtrAudioEngine *e) {
    CtrAudioResult r=engine_argument(e);
    if(r!=CTR_AUDIO_OK)return r;
    for(unsigned i=0;i<CTR_AUDIO_VOICES;++i) {
        const CtrAudioVoice *v=&e->voices[i];
        if(v->gain[0]>CTR_AUDIO_GAIN_ONE || v->gain[1]>CTR_AUDIO_GAIN_ONE)return CTR_AUDIO_STATE;
        if(!v->bound) {
            if(v->data || v->bytes || v->playing || v->pair_valid)return CTR_AUDIO_STATE;
            continue;
        }
        if(!v->data)return CTR_AUDIO_STATE;
        if(v->bytes>CTR_CSTM_MAX_BYTES || !span(v->data,v->bytes))return CTR_AUDIO_BOUNDS;
        if(!separate((uintptr_t)e,sizeof(*e),(uintptr_t)v->data,v->bytes))return CTR_AUDIO_OVERLAP;
        CtrCstmInfo info;
        r=cstm_result(ctr_cstm_info(v->data,v->bytes,&info));
        if(r!=CTR_AUDIO_OK)return r;
        if(!same_info(&info,&v->info))return CTR_AUDIO_STATE;
        if(info.sample_rate<8000 || info.sample_rate>96000)return CTR_AUDIO_UNSUPPORTED;
        if(v->position>info.samples || v->decoded>info.samples || v->phase>=CTR_AUDIO_OUTPUT_RATE)
            return CTR_AUDIO_STATE;
        if(v->pair_valid) {
            if(v->pair_position>=info.samples || v->pair_position>v->position ||
               v->position-v->pair_position>2u || v->decoded!=v->pair_position+1u+(v->pair_position+1u<info.samples))return CTR_AUDIO_STATE;
        } else if(v->decoded!=v->position || v->pair_position!=v->position)return CTR_AUDIO_STATE;
    }
    return CTR_AUDIO_OK;
}
static CtrAudioResult voice_argument(const CtrAudioEngine *e,unsigned voice) {
    CtrAudioResult r=validate(e);
    if(r!=CTR_AUDIO_OK)return r;
    return voice<CTR_AUDIO_VOICES?CTR_AUDIO_OK:CTR_AUDIO_BOUNDS;
}
static CtrAudioResult load_block(const CtrAudioVoice *v,BlockCache *cache,uint32_t block) {
    if(cache->loaded && cache->block==block)return CTR_AUDIO_OK;
    for(unsigned c=0;c<v->info.channels;++c) {
        CtrAudioResult r=cstm_result(ctr_cstm_block(v->data,v->bytes,block,c,&cache->channels[c]));
        if(r!=CTR_AUDIO_OK)return r;
    }
    cache->loaded=true;cache->block=block;
    return CTR_AUDIO_OK;
}
static CtrAudioResult decode_frame(CtrAudioVoice *v,BlockCache *cache,int16_t output[2]) {
    if(v->decoded>=v->info.samples)return CTR_AUDIO_STATE;
    uint32_t block=v->decoded/v->info.block_samples;
    CtrAudioResult r=load_block(v,cache,block);
    if(r!=CTR_AUDIO_OK)return r;
    CtrDspAdpcmState state[2];
    for(unsigned c=0;c<v->info.channels;++c) {
        CtrCstmBlock *b=&cache->channels[c];
        state[c].sample=v->decoded-b->first_sample;
        state[c].history1=v->history[c][0];state[c].history2=v->history[c][1];
        r=codec_result(ctr_dsp_adpcm_decode(b->encoded,b->bytes,b->coefficients,
            &state[c],&output[c],1,1));
        if(r!=CTR_AUDIO_OK)return r;
    }
    for(unsigned c=0;c<v->info.channels;++c) {
        v->history[c][0]=state[c].history1;v->history[c][1]=state[c].history2;
    }
    if(v->info.channels==1)output[1]=output[0];
    ++v->decoded;count(&v->decoded_frames,1);
    return CTR_AUDIO_OK;
}
/* Prepare the two samples bracketing position. Decode every skipped source frame
 * so predictor histories remain continuous even when downsampling. */
static CtrAudioResult prepare_pair(CtrAudioVoice *v,BlockCache *cache) {
    if(!v->pair_valid) {
        int16_t value[2];CtrAudioResult r=decode_frame(v,cache,value);
        if(r!=CTR_AUDIO_OK)return r;
        for(unsigned c=0;c<2;++c)v->pair[c][0]=value[c];
        if(v->decoded<v->info.samples) {
            r=decode_frame(v,cache,value);if(r!=CTR_AUDIO_OK)return r;
        }
        for(unsigned c=0;c<2;++c)v->pair[c][1]=value[c];
        v->pair_position=v->position;v->pair_valid=true;
    }
    while(v->pair_position<v->position) {
        int16_t value[2];
        for(unsigned c=0;c<2;++c)value[c]=v->pair[c][1];
        if(v->decoded<v->info.samples) {
            CtrAudioResult r=decode_frame(v,cache,value);if(r!=CTR_AUDIO_OK)return r;
        }
        for(unsigned c=0;c<2;++c) {
            v->pair[c][0]=v->pair[c][1];v->pair[c][1]=value[c];
        }
        ++v->pair_position;
    }
    return CTR_AUDIO_OK;
}
/* floor((numerator + denominator/2)/denominator), without signed shifts or
 * implementation-defined negative division rounding. denominator is positive. */
static int32_t rounded(int64_t numerator,int32_t denominator) {
    int64_t n=numerator+denominator/2;
    int64_t q=n/denominator;
    if(n<0 && n%denominator)--q;
    return (int32_t)q;
}
static CtrAudioResult run(CtrAudioEngine *e,int16_t *output,size_t frames) {
    BlockCache cache[CTR_AUDIO_VOICES]={0};
    for(size_t frame=0;frame<frames;++frame) {
        int32_t contribution[CTR_AUDIO_VOICES][2]={{0}};
        int32_t sum[2]={0};
        for(unsigned i=0;i<CTR_AUDIO_VOICES;++i) {
            CtrAudioVoice *v=&e->voices[i];
            if(!v->bound || !v->playing)continue;
            if(v->position>=v->info.samples) { v->playing=false;continue; }
            CtrAudioResult r=prepare_pair(v,&cache[i]);if(r!=CTR_AUDIO_OK)return r;
            for(unsigned c=0;c<2;++c) {
                int64_t numerator=(int64_t)v->pair[c][0]*(CTR_AUDIO_OUTPUT_RATE-v->phase)+
                    (int64_t)v->pair[c][1]*v->phase;
                int32_t sample=rounded(numerator,CTR_AUDIO_OUTPUT_RATE);
                contribution[i][c]=rounded((int64_t)sample*v->gain[c],CTR_AUDIO_GAIN_ONE);
                sum[c]+=contribution[i][c];
            }
            count(&v->rendered_frames,1);
            uint32_t phase=v->phase+v->info.sample_rate;
            uint64_t position=(uint64_t)v->position+phase/CTR_AUDIO_OUTPUT_RATE;
            v->phase=phase%CTR_AUDIO_OUTPUT_RATE;
            v->position=position<v->info.samples?(uint32_t)position:v->info.samples;
            if(v->position==v->info.samples)v->playing=false;
        }
        for(unsigned c=0;c<2;++c) {
            int32_t sample=sum[c];
            if(sample>32767 || sample<-32768) {
                count(&e->clipped_samples,1);
                for(unsigned i=0;i<CTR_AUDIO_VOICES;++i)
                    if((sample>32767 && contribution[i][c]>0) ||
                       (sample<-32768 && contribution[i][c]<0))count(&e->voices[i].clipped_samples,1);
                sample=sample>32767?32767:-32768;
            }
            if(output)output[frame*2+c]=(int16_t)sample;
        }
    }
    count(&e->rendered_frames,frames);return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_init(CtrAudioEngine *engine) {
    if(!engine)return CTR_AUDIO_ARGUMENT;
    if(!span(engine,sizeof(*engine)))return CTR_AUDIO_BOUNDS;
    CtrAudioEngine e={0};e.guard=AUDIO_GUARD;
    for(unsigned i=0;i<CTR_AUDIO_VOICES;++i)e.voices[i].gain[0]=e.voices[i].gain[1]=CTR_AUDIO_GAIN_ONE;
    *engine=e;return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_bind(CtrAudioEngine *engine,unsigned voice,const uint8_t *data,size_t bytes) {
    CtrAudioResult r=voice_argument(engine,voice);if(r!=CTR_AUDIO_OK)return r;
    if(!data && bytes)return CTR_AUDIO_ARGUMENT;
    if(data && !bytes)return CTR_AUDIO_BOUNDS;
    if(bytes>CTR_CSTM_MAX_BYTES || !span(data,bytes))return CTR_AUDIO_BOUNDS;
    if(data && !separate((uintptr_t)engine,sizeof(*engine),(uintptr_t)data,bytes))return CTR_AUDIO_OVERLAP;
    CtrAudioVoice v={0};v.gain[0]=v.gain[1]=CTR_AUDIO_GAIN_ONE;
    if(data) {
        r=cstm_result(ctr_cstm_info(data,bytes,&v.info));if(r!=CTR_AUDIO_OK)return r;
        if(v.info.sample_rate<8000 || v.info.sample_rate>96000)return CTR_AUDIO_UNSUPPORTED;
        v.data=data;v.bytes=bytes;v.bound=true;
        for(unsigned c=0;c<v.info.channels;++c) {
            CtrCstmBlock b;r=cstm_result(ctr_cstm_block(data,bytes,0,c,&b));if(r!=CTR_AUDIO_OK)return r;
            v.history[c][0]=b.state.history1;v.history[c][1]=b.state.history2;
        }
    }
    engine->voices[voice]=v;return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_gain(CtrAudioEngine *engine,unsigned voice,uint32_t left,uint32_t right) {
    CtrAudioResult r=voice_argument(engine,voice);if(r!=CTR_AUDIO_OK)return r;
    if(left>CTR_AUDIO_GAIN_ONE || right>CTR_AUDIO_GAIN_ONE)return CTR_AUDIO_BOUNDS;
    engine->voices[voice].gain[0]=left;engine->voices[voice].gain[1]=right;return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_play(CtrAudioEngine *engine,unsigned voice,bool playing) {
    CtrAudioResult r=voice_argument(engine,voice);if(r!=CTR_AUDIO_OK)return r;
    if(!engine->voices[voice].bound)return CTR_AUDIO_STATE;
    engine->voices[voice].playing=playing && engine->voices[voice].position<engine->voices[voice].info.samples;
    return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_seek(CtrAudioEngine *engine,unsigned voice,uint32_t sample) {
    CtrAudioResult r=voice_argument(engine,voice);if(r!=CTR_AUDIO_OK)return r;
    CtrAudioVoice v=engine->voices[voice];
    if(!v.bound)return CTR_AUDIO_STATE;
    if(sample>v.info.samples)return CTR_AUDIO_BOUNDS;
    v.position=sample;v.phase=0;v.pair_position=sample;v.pair_valid=false;
    if(sample==v.info.samples) { v.decoded=sample;v.playing=false; }
    else {
        uint32_t block=sample/v.info.block_samples;
        BlockCache cache={0};r=load_block(&v,&cache,block);if(r!=CTR_AUDIO_OK)return r;
        v.decoded=block*v.info.block_samples;
        for(unsigned c=0;c<v.info.channels;++c) {
            v.history[c][0]=cache.channels[c].state.history1;
            v.history[c][1]=cache.channels[c].state.history2;
        }
        while(v.decoded<sample) {
            int16_t ignored[2];r=decode_frame(&v,&cache,ignored);if(r!=CTR_AUDIO_OK)return r;
        }
    }
    engine->voices[voice]=v;return CTR_AUDIO_OK;
}
CtrAudioResult ctr_audio_render(CtrAudioEngine *engine,int16_t *output,size_t capacity,size_t frames) {
    CtrAudioResult r=validate(engine);if(r!=CTR_AUDIO_OK)return r;
    if(frames>CTR_AUDIO_MAX_FRAMES || frames>capacity)return CTR_AUDIO_BOUNDS;
    if(frames && !output)return CTR_AUDIO_ARGUMENT;
    size_t bytes=frames*2*sizeof(*output);
    if(!span(output,bytes))return CTR_AUDIO_BOUNDS;
    if(bytes && !separate((uintptr_t)engine,sizeof(*engine),(uintptr_t)output,bytes))return CTR_AUDIO_OVERLAP;
    for(unsigned i=0;i<CTR_AUDIO_VOICES;++i) {
        CtrAudioVoice *v=&engine->voices[i];
        if(bytes && v->bound && !separate((uintptr_t)v->data,v->bytes,(uintptr_t)output,bytes))return CTR_AUDIO_OVERLAP;
    }
    CtrAudioEngine dry=*engine;
    r=run(&dry,NULL,frames);if(r!=CTR_AUDIO_OK)return r;
    /* Source lifetime/immutability is a caller requirement. The successful dry
     * pass makes this identical second simulation infallible under that contract. */
    CtrAudioEngine committed=*engine;
    r=run(&committed,output,frames);if(r!=CTR_AUDIO_OK)return r;
    *engine=committed;return CTR_AUDIO_OK;
}
const char *ctr_audio_result_name(CtrAudioResult result) {
    switch(result) {
    case CTR_AUDIO_OK:return "ok";
    case CTR_AUDIO_ARGUMENT:return "argument";
    case CTR_AUDIO_BOUNDS:return "bounds";
    case CTR_AUDIO_FORMAT:return "format";
    case CTR_AUDIO_UNSUPPORTED:return "unsupported";
    case CTR_AUDIO_OVERLAP:return "overlap";
    case CTR_AUDIO_PREDICTOR:return "predictor";
    case CTR_AUDIO_STATE:return "state";
    default:return "invalid";
    }
}
