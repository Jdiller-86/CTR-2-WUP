/* Original bounded NDSP audio HLE. The pinned libctru interface defines the
 * pipe and source update protocol. */
#include "ctr_dsp.h"
#include "ctr_dsp_adpcm.h"
#include "ctr_heap.h"
#include "ctr_kernel.h"
#include "ctr_sha256.h"
#include <stdlib.h>
#include <string.h>

const uint8_t ctr_dsp_component[16]={'C','T','R','2','W','U','P','-','P','C','M','-','H','L','E',1};
#define BANK 0x20000u
#define CHANNEL 0x100u
#define STATUS 0x2000u
#define COEFFICIENTS 0x2200u
#define MASTER 0x2600u
#define CAPTURE 0x4000u
#define AUXILIARY 0x5000u
#define FRAME_SPAN 0x5000u
#define COMPONENT_COMPRESSOR 0x6800u
#define COMPONENT_COMPRESSOR_BYTES 0x1a40u
#define COMPONENT_DEBUG 0x8300u
#define COMPONENT_DEBUG_BYTES 0x260u
#define COMPONENT_SPAN 0x9000u
#define STRIDE 0xc0u
#define PROBE_MAX_SAMPLES 65536u
#define COMPONENT_MAX_SAMPLES (1024u*1024u)
#define INVALID UINT32_C(0xd8e007f7)
#define NO_MEMORY UINT32_C(0xd86007f3)
#define BUSY UINT32_C(0xd8a007f3)
#define ALLOWED UINT32_C(0x7e6f0014)
enum { AUX_BUSES=2u, AUX_LANES=4u, AUX_BUS_BYTES=AUX_LANES*CTR_DSP_FRAME_SAMPLES*4u };
_Static_assert(AUXILIARY+AUX_BUSES*AUX_BUS_BYTES<=COMPONENT_COMPRESSOR,"DSP mixer regions overlap");
/* Counter, channel commands/status, coefficients, master, DSP state, capture,
 * auxiliary buffers and unknown state. Word addresses; independent owned banks. */
static const uint16_t variables[]={0,CHANNEL/2,STATUS/2,COEFFICIENTS/2,MASTER/2,0x2800/2,CAPTURE/2,AUXILIARY/2,0x6000/2};
typedef struct {
    uint32_t address,count,position,bytes;uint16_t sequence;bool looping,continue_history;unsigned allocation;
    CtrDspAdpcmState decoder,initial;
} buffer;
typedef enum { PCM16, PCM8, ADPCM } sample_format;
typedef struct {
    buffer current,queued[4]; unsigned head,count,next_slot;
    uint16_t sync,last;bool playing;
    uint64_t step;uint32_t phase;unsigned interpolation;
    sample_format format;int16_t coefficients[16];
    int32_t gain[2],aux_gain[AUX_BUSES][AUX_LANES];
    bool mono_enabled,mono_configured,biquad_enabled,biquad_configured;
    int16_t mono_b0,mono_a1,mono_output;
    int16_t biquad[5];
    int16_t input1,input2,output1,output2;
} voice;
struct ctr_guest_dsp {
    ctr_dsp_statistics stats;unsigned owner,irq,sem;bool irq_live,sem_live,verified_component;
    uint16_t mask,counter;unsigned bank;
    uint64_t deadline,remainder;
    uint8_t pipe[4+sizeof(variables)];unsigned pipe_at,pipe_size;bool reply_ready;
    voice voices[CTR_DSP_CHANNELS];int32_t master,aux_volume[AUX_BUSES];bool aux_enabled[AUX_BUSES];
    int16_t output[CTR_DSP_OUTPUT_FRAMES][CTR_DSP_FRAME_SAMPLES*2];
    unsigned first,count;
};
static uint32_t le(const uint8_t *p,unsigned n) { uint32_t v=0;for(unsigned i=0;i<n;++i)v|=(uint32_t)p[i]<<(8*i);return v; }
static int16_t signed_half(uint32_t value) { return (int16_t)(value<32768u?(int32_t)value:(int32_t)value-65536); }
static int64_t signed_word(uint32_t value) { return (int64_t)value-(value>>31?INT64_C(0x100000000):0); }
static void put(uint8_t *p,unsigned n,uint32_t v) { for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i)); }
static bool zeroed(const uint8_t *p,size_t n) {
    for(size_t i=0;i<n;++i)if(p[i])return false;
    return true;
}
static uint32_t rotate(uint32_t v) { return v<<16|v>>16; }
static void add(uint64_t *v,uint64_t n) { *v=UINT64_MAX-*v<n?UINT64_MAX:*v+n; }
static bool overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return n && m && (x<=y?y-x<n:x-y<m);
}
static bool host_alias(const ctr_guest *g,const void *p,size_t n) {
    if(overlap(p,n,g,sizeof(*g)) || (g->dsp && overlap(p,n,g->dsp,sizeof(*g->dsp))))return true;
    for(size_t i=0;i<g->memory.count;++i)if(overlap(p,n,g->memory.maps[i].storage,g->memory.maps[i].size))return true;
    return false;
}
static bool fail(ctr_guest *g,uint32_t detail,ctr_mem_result memory) {
    if(g->dsp) { add(&g->dsp->stats.rejected,1);g->dsp->stats.detail=detail; }
    g->fault=memory==CTR_MEM_OK?CTR_CPU_UNSUPPORTED:CTR_CPU_MEMORY_FAULT;
    g->fault_detail=detail;g->cpu.memory_fault=memory;g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,g->fault,detail);return false;
}
static bool range(ctr_guest *g,uint32_t address,uint32_t size,unsigned rights) {
    ctr_mem_result r=ctr_memory_check_range(&g->memory,address,size,rights);
    return r==CTR_MEM_OK || fail(g,address,r);
}
static bool guest_alias(const ctr_guest *g,uint32_t address,uint32_t size,uint32_t other,uint32_t bytes) {
    uint64_t end=(uint64_t)address+size,stop=(uint64_t)other+bytes;
    for(size_t i=0;i<g->memory.count;++i)for(size_t j=0;j<g->memory.count;++j) {
        const ctr_mapping *a=&g->memory.maps[i],*b=&g->memory.maps[j];
        uint64_t lo=address>a->base?address:a->base,hi=end<(uint64_t)a->base+a->size?end:(uint64_t)a->base+a->size;
        uint64_t bl=other>b->base?other:b->base,bh=stop<(uint64_t)b->base+b->size?stop:(uint64_t)b->base+b->size;
        if(lo<hi && bl<bh && overlap(a->storage+(size_t)(lo-a->base),(size_t)(hi-lo),b->storage+(size_t)(bl-b->base),(size_t)(bh-bl)))return true;
    }
    return false;
}
/* Physical pointers must still name the same live linear owner after splits,
 * frees and reallocations. No source pointer is retained across a guest entry. */
static bool source(ctr_guest *g,uint32_t physical,uint32_t bytes,uint32_t *address) {
    if(physical<0x20000000u || (uint64_t)physical+bytes>0x28000000u || (physical&1u))return fail(g,physical,CTR_MEM_UNMAPPED);
    uint32_t at=CTR_LINEAR_BASE+physical-0x20000000u;
    const ctr_guest_allocation *owner=NULL;
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if(a->storage && a->linear && at>=a->base && (uint64_t)at+bytes<=(uint64_t)a->base+a->size)owner=a;
    }
    if(!owner)return fail(g,physical,CTR_MEM_UNMAPPED);
    if(!range(g,at,bytes,CTR_MEM_READ))return false;
    uint64_t cursor=at,end=cursor+bytes;
    while(cursor<end) {
        const ctr_mapping *m=NULL;
        for(size_t i=0;i<g->memory.count;++i)if(cursor>=g->memory.maps[i].base && cursor<(uint64_t)g->memory.maps[i].base+g->memory.maps[i].size)m=&g->memory.maps[i];
        if(!m || m->storage+(size_t)(cursor-m->base)!=owner->storage+(size_t)(cursor-owner->base))return fail(g,physical,CTR_MEM_OVERLAP);
        uint64_t next=(uint64_t)m->base+m->size;cursor=next<end?next:end;
    }
    *address=at;return true;
}
/* Exact binary32 -> signed Q15, nearest ties positive. No host float/endianness. */
static bool gain(uint32_t bits,int32_t *out) {
    bool negative=bits>>31;unsigned exponent=(bits>>23)&255u;uint32_t fraction=bits&0x7fffffu;
    if(exponent>127 || (exponent==127 && fraction))return false;
    uint32_t mantissa=exponent?fraction|0x800000u:fraction;
    int shift=exponent?(int)exponent-135:-134;uint32_t magnitude=0;
    if(shift>=0)magnitude=mantissa<<(unsigned)shift;
    else if(shift>-32) {
        unsigned n=(unsigned)-shift;uint32_t q=mantissa>>n,rem=mantissa&((UINT32_C(1)<<n)-1),half=UINT32_C(1)<<(n-1);
        magnitude=q+(rem>half || (!negative && rem==half));
    }
    *out=negative?-(int32_t)magnitude:(int32_t)magnitude;return true;
}
static int64_t rounded(int64_t v) { v+=16384;return v>=0?v/32768:-((-v+32767)/32768); }
static int16_t clipped(int64_t v) { return (int16_t)(v>32767?32767:v<-32768?-32768:v); }
static int32_t filter_sample(voice *v,int32_t sample) {
    if(v->mono_enabled) {
        int64_t sum=(int64_t)v->mono_b0*sample+(int64_t)v->mono_a1*v->mono_output;
        v->mono_output=clipped(sum>=0?sum/32768:-((-sum+32767)/32768));
        sample=v->mono_output;
    }
    if(!v->biquad_enabled)return sample;
    /* Wire order is a2, a1, b2, b1, b0. Feedback coefficients are signed Q14. */
    int64_t sum=(int64_t)v->biquad[0]*v->output2+(int64_t)v->biquad[1]*v->output1+
                (int64_t)v->biquad[2]*v->input2+(int64_t)v->biquad[3]*v->input1+
                (int64_t)v->biquad[4]*sample;
    int16_t output=clipped(sum>=0?sum/16384:-((-sum+16383)/16384));
    v->input2=v->input1;v->input1=(int16_t)sample;
    v->output2=v->output1;v->output1=output;
    return output;
}
/* Positive binary32 ratios from 1/512 through 4 convert exactly to Q32. */
static bool rate(uint32_t bits,bool verified_component,uint64_t *step) {
    uint32_t minimum=verified_component?0x3b000000u:0x3e000000u;
    if(bits<minimum || bits>0x40800000u)return false;
    *step=(uint64_t)((bits&0x7fffffu)|0x800000u)<<((bits>>23)-118u);
    return true;
}
static bool sample_at(ctr_guest *g,const buffer *b,sample_format format,uint32_t position,int32_t *out) {
    uint32_t address,sample;
    unsigned width=format==PCM8?1u:2u;
    if(!source(g,b->address,b->bytes,&address))return false;
    if(ctr_memory_read(&g->memory,address+position*width,width,CTR_MEM_READ,&sample)!=CTR_MEM_OK)
        return fail(g,address,CTR_MEM_UNMAPPED);
    *out=signed_half(width==1u?sample<<8:sample);return true;
}
static bool adpcm_sample(ctr_guest *g,const int16_t *coefficients,buffer *b,uint32_t position,int32_t *out) {
    if(position>=b->count || b->decoder.sample>position+1u)return fail(g,b->address,CTR_MEM_OK);
    if(b->decoder.sample==position+1u) { *out=b->decoder.history1;return true; }
    uint32_t address;
    if(!source(g,b->address,b->bytes,&address))return false;
    /* Advance history through skipped source samples. A frame-local copy also
     * handles the final partial frame without retaining a guest host pointer. */
    while(b->decoder.sample<=position) {
        unsigned within=(unsigned)(b->decoder.sample%14u),count=14u-within;
        unsigned needed=position-(unsigned)b->decoder.sample+1u;
        if(count>needed)count=needed;
        uint32_t frame_at=address+(uint32_t)(b->decoder.sample/14u)*8u;
        unsigned bytes=2u+(within+count-1u)/2u;
        uint8_t encoded[8];int16_t samples[14];
        if(ctr_memory_read_bytes(&g->memory,frame_at,encoded,bytes)!=CTR_MEM_OK)
            return fail(g,frame_at,CTR_MEM_UNMAPPED);
        CtrDspAdpcmState state=b->decoder;state.sample=within;
        if(ctr_dsp_adpcm_decode(encoded,bytes,coefficients,&state,samples,14,count)!=CTR_DSP_ADPCM_OK)
            return fail(g,frame_at,CTR_MEM_OK);
        b->decoder.sample+=count;
        b->decoder.history1=state.history1;b->decoder.history2=state.history2;
    }
    *out=b->decoder.history1;return true;
}
static CtrDspAdpcmState adpcm_start(const buffer *next,const buffer *previous) {
    if(!next->continue_history)return next->initial;
    return (CtrDspAdpcmState){0,previous->decoder.history1,previous->decoder.history2};
}
static bool voice_sample(ctr_guest *g,voice *v,int32_t *out) {
    buffer *b=&v->current;
    bool adpcm=v->format==ADPCM;
    bool sampled=adpcm?adpcm_sample(g,v->coefficients,b,b->position,out):sample_at(g,b,v->format,b->position,out);
    if(!sampled)return false;
    if(v->interpolation!=1 || !v->phase)return true;
    const buffer *next=b;uint32_t position=b->position+1;
    if(position==b->count) {
        if(b->looping)position=0;
        else if(v->count) { next=&v->queued[v->head];position=0; }
        else return true; /* Hold the last sample for its fractional tail. */
    }
    int32_t following;
    if(adpcm) {
        buffer ahead=*next;
        if(!position)ahead.decoder=adpcm_start(next,b);
        if(!adpcm_sample(g,v->coefficients,&ahead,position,&following))return false;
    } else if(!sample_at(g,next,v->format,position,&following))return false;
    int64_t value=(int64_t)*out*(int64_t)(UINT64_C(0x100000000)-v->phase)+
                  (int64_t)following*v->phase+INT64_C(0x80000000);
    *out=(int32_t)(value>=0?value/INT64_C(0x100000000):
                  -((-value+INT64_C(0xffffffff))/INT64_C(0x100000000)));
    return true;
}
static bool decode_buffer(ctr_guest *g,const uint8_t *p,bool initial,sample_format format,buffer *b) {
    uint32_t count=rotate(le(p+4,4)),address=rotate(le(p,4));
    unsigned more=initial?le(p+16,2):p[15];uint16_t sequence=(uint16_t)le(p+(initial?18:16),2);
    unsigned history=initial?more&1u:p[14];
    unsigned limit=g->dsp->verified_component?COMPONENT_MAX_SAMPLES:PROBE_MAX_SAMPLES;
    bool adpcm=format==ADPCM;
    bool continue_history=g->dsp->verified_component && adpcm && !initial && !more && !history;
    /* The verified PCM8 format can retain the ADPCM history bit. */
    if(!count || count>limit || !sequence || more>(initial?3u:1u) || history>1u ||
       (adpcm && !history && !continue_history) || (format==PCM16 && history))
        return fail(g,address,CTR_MEM_OK);
    uint32_t bytes=adpcm?(count-1)/14*8+2+(count-1)%14/2:count*(format==PCM8?1u:2u);
    uint32_t ignored;if(!source(g,address,bytes,&ignored))return false;
    unsigned allocation=CTR_GUEST_MAX_ALLOCATIONS;
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if(a->storage && a->linear && ignored>=a->base && (uint64_t)ignored+bytes<=(uint64_t)a->base+a->size)allocation=i;
    }
    if(allocation==CTR_GUEST_MAX_ALLOCATIONS)return fail(g,address,CTR_MEM_UNMAPPED);
    *b=(buffer){.address=address,.count=count,.bytes=bytes,.sequence=sequence,
                .looping=initial?(more&2u)!=0:more!=0,.continue_history=continue_history,.allocation=allocation};
    if(adpcm) {
        const uint8_t *state=p+(initial?10:8);uint32_t header;
        if(ctr_memory_read(&g->memory,ignored,1,CTR_MEM_READ,&header)!=CTR_MEM_OK)
            return fail(g,address,CTR_MEM_UNMAPPED);
        if((header>>4)>7 || (!continue_history && le(state,2)!=header))return fail(g,address,CTR_MEM_OK);
        if(!continue_history) {
            b->initial=(CtrDspAdpcmState){0,signed_half(le(state+2,2)),signed_half(le(state+4,2))};
            b->decoder=b->initial;
        }
    }
    return true;
}
static void pins(ctr_guest *g,const voice *voices,bool retain) {
    for(unsigned c=0;c<CTR_DSP_CHANNELS;++c) {
        const voice *v=&voices[c];
        for(unsigned i=0;i<=v->count;++i) {
            const buffer *b=i?&v->queued[(v->head+i-1)%4]:&v->current;
            if(b->sequence) {
                unsigned *p=&g->allocations[b->allocation].pins;
                if(retain)++*p;else if(*p)--*p;
            }
        }
    }
}
static void reset(ctr_guest *g) {
    struct ctr_guest_dsp *d=g->dsp;if(!d)return;
    pins(g,d->voices,false);
    if(d->irq_live)ctr_guest_object_release(g,CTR_HANDLE_EVENT,d->irq);
    if(d->sem_live)ctr_guest_object_release(g,CTR_HANDLE_EVENT,d->sem);
    memset(d,0,sizeof(*d));
}
void ctr_dsp_session_closed(ctr_guest *g,unsigned session) { if(g->dsp && g->dsp->stats.loaded && g->dsp->owner==session)reset(g); }
void ctr_dsp_close(ctr_guest *g) { reset(g);free(g->dsp);g->dsp=NULL; }
static bool frame(ctr_guest *g) {
    struct ctr_guest_dsp *d=g->dsp;
    if(d->count==CTR_DSP_OUTPUT_FRAMES)return fail(g,0x44535051u,CTR_MEM_OK);
    uint32_t offset=d->bank*BANK;
    unsigned span=d->verified_component?COMPONENT_SPAN:FRAME_SPAN;
    /* Memory mappings are caller-editable in the public API. Require the exact
     * original backing, including every command/status/capture byte. */
    for(unsigned bank=0;bank<2;++bank) {
        if(!range(g,CTR_DSP_RAM+bank*BANK,span,CTR_MEM_READ|CTR_MEM_WRITE))return false;
        for(unsigned i=0;i<span;i+=4096) {
            bool found=false;uint32_t address=CTR_DSP_RAM+bank*BANK+i;
            for(size_t j=0;j<g->memory.count;++j) {
                const ctr_mapping *m=&g->memory.maps[j];
                if(address>=m->base && (uint64_t)address+4096<=(uint64_t)m->base+m->size &&
                   m->storage+(address-m->base)==g->storage[7]+bank*BANK+i)found=true;
            }
            if(!found)return fail(g,address,CTR_MEM_OVERLAP);
        }
    }
    const uint8_t *ram=g->storage[7]+offset;
    if(le(ram,2)!=d->counter)return fail(g,CTR_DSP_RAM+offset,CTR_MEM_OK);
    voice voices[CTR_DSP_CHANNELS];memcpy(voices,d->voices,sizeof(voices));
    ctr_dsp_statistics stats=d->stats;int32_t master=d->master,aux_volume[AUX_BUSES];
    bool aux_enabled[AUX_BUSES];
    memcpy(aux_volume,d->aux_volume,sizeof(aux_volume));
    memcpy(aux_enabled,d->aux_enabled,sizeof(aux_enabled));
    uint32_t mf=le(ram+MASTER,4);
    /* The verified stereo profile accepts the SDK's inactive effect setup.
     * The mixer saturates final output for either supported clip setting. */
    const uint32_t master_flags=d->verified_component?0xff03bfc0u:0x1c0183c0u;
    if((mf&~master_flags) || ((mf&0x10000u) && !gain(le(ram+MASTER+4,4),&master)) ||
       ((mf&0x04000000u) && le(ram+MASTER+22,2)!=1) ||
       ((mf&0x08000000u) && le(ram+MASTER+24,2)>(d->verified_component?1u:0u)) ||
       ((mf&0x8000u) && le(ram+MASTER+16,2)!=2))return fail(g,CTR_DSP_RAM+offset+MASTER,CTR_MEM_OK);
    for(unsigned bus=0;bus<AUX_BUSES;++bus) {
        if(mf&0x300u) {
            unsigned enabled=le(ram+MASTER+40u+bus*2u,2);
            if(enabled>(d->verified_component?1u:0u))return fail(g,CTR_DSP_RAM+offset+MASTER,CTR_MEM_OK);
            if(mf&(0x100u<<bus))aux_enabled[bus]=enabled!=0;
        }
        if(d->verified_component && (((mf&(0x01000000u<<bus)) && !gain(le(ram+MASTER+8u+bus*4u,4),&aux_volume[bus])) ||
           (aux_enabled[bus] && le(ram+MASTER+36u+bus*2u,2))))return fail(g,CTR_DSP_RAM+offset+MASTER,CTR_MEM_OK);
    }
    if(d->verified_component && (le(ram+MASTER+0xc0,4) ||
       ((mf&0x0400u) && le(ram+MASTER+0x2c,4)>1u) || ((mf&0x0800u) && le(ram+MASTER+0x40,4)>1u) ||
       ((mf&0x1000u) && le(ram+MASTER+0x54,4)>1u) || ((mf&0x2000u) && le(ram+MASTER+0x88,4)>1u) ||
       ((mf&0x20000u) && !zeroed(ram+COMPONENT_COMPRESSOR,COMPONENT_COMPRESSOR_BYTES))))
        return fail(g,CTR_DSP_RAM+offset+MASTER,CTR_MEM_OK);
    const uint32_t channel_flags=ALLOWED|(d->verified_component?0x01800000u:0u);
    for(unsigned c=0;c<CTR_DSP_CHANNELS;++c) {
        const uint8_t *p=ram+CHANNEL+c*STRIDE;uint32_t flags=le(p,4);voice *v=&voices[c];
        if(flags&~channel_flags)return fail(g,c,CTR_MEM_OK);
        if(flags&0x20000000u) { memset(v,0,sizeof(*v));v->gain[0]=v->gain[1]=32768;v->step=UINT64_C(0x100000000); }
        if(flags&0x10000000u) { v->sync=(uint16_t)le(p+0xa2,2);v->current=(buffer){0};v->head=v->count=0;v->last=0;v->phase=0; }
        if(flags&4u) {
            if(v->current.sequence && v->format==ADPCM)return fail(g,c,CTR_MEM_OK);
            for(unsigned i=0;i<16;++i)v->coefficients[i]=signed_half(le(ram+COEFFICIENTS+c*32+i*2,2));
        }
        if((flags&0x40000u) && !rate(le(p+0x34,4),d->verified_component,&v->step))return fail(g,c,CTR_MEM_OK);
        if(flags&0x20000u) {
            if(p[0x38]>2 || p[0x39]>2)return fail(g,c,CTR_MEM_OK);
            v->interpolation=p[0x38];
            if(d->verified_component && !v->interpolation)v->interpolation=1;
        }
        if(flags&0x400000u) {
            unsigned filter=le(p+0x3a,2);
            if(filter>(d->verified_component?3u:0u))return fail(g,c,CTR_MEM_OK);
            v->mono_enabled=(filter&1u)!=0;
            v->biquad_enabled=(filter&2u)!=0;
            if(!v->mono_enabled)v->mono_output=0;
            if(!v->biquad_enabled)v->input1=v->input2=v->output1=v->output2=0;
        }
        if(flags&0x00800000u) {
            v->mono_b0=signed_half(le(p+0x3c,2));v->mono_a1=signed_half(le(p+0x3e,2));
            v->mono_configured=true;
        }
        if(flags&0x01000000u) {
            for(unsigned i=0;i<5;++i)v->biquad[i]=signed_half(le(p+0x40+i*2,2));
            v->biquad_configured=true;
        }
        if((v->mono_enabled && !v->mono_configured) || (v->biquad_enabled && !v->biquad_configured))
            return fail(g,c,CTR_MEM_OK);
        if(flags&0xe000000u) {
            for(unsigned i=0;i<12;++i) {
                int32_t value;if(!gain(le(p+4+i*4,4),&value))return fail(g,c,CTR_MEM_OK);
                if(i<2) {
                    if(!d->verified_component || (flags&0x02000000u))v->gain[i]=value;
                } else if(d->verified_component && i>=AUX_LANES) {
                    unsigned bus=i/AUX_LANES-1u;
                    if(flags&(0x04000000u<<bus))v->aux_gain[bus][i%AUX_LANES]=value;
                } else if(value)return fail(g,c,CTR_MEM_OK);
            }
        }
        if(flags&0x10000u) { if(le(p+0xa0,2)>1)return fail(g,c,CTR_MEM_OK);v->playing=le(p+0xa0,2)!=0; }
        if(flags&0x40200010u) {
            unsigned format=le(p+0xb4,2);
            /* This verified component also supplies bit 4 with mono ADPCM.
               The channel count and codec occupy bits 0 through 3. */
            if(d->verified_component && format==0x19u)format=9u;
            if((flags&0x40200010u)!=0x40200010u || (format!=5 && format!=9 &&
               !(d->verified_component && format==1)))return fail(g,c,CTR_MEM_OK);
            v->format=format==9?ADPCM:format==1?PCM8:PCM16;
            if(!decode_buffer(g,p+0xac,true,v->format,&v->current))return false;
            v->count=v->head=v->next_slot=0;v->phase=0;add(&stats.queued,1);
        }
        if(flags&0x80000u) {
            unsigned mask=le(p+0x4a,2);if((mask && !v->current.sequence) || (mask&~15u))return fail(g,c,CTR_MEM_OK);
            for(unsigned slots=0;mask && slots<4;++slots) {
                unsigned i=v->next_slot;
                if(!(mask&(1u<<i)))return fail(g,c,CTR_MEM_OK);
                mask&=~(1u<<i);v->next_slot=(i+1)%4;
                if(v->count==4)return fail(g,c,CTR_MEM_OK);
                buffer b;if(!decode_buffer(g,p+0x4c+i*20,false,v->format,&b))return false;
                if(b.sequence==v->current.sequence)return fail(g,c,CTR_MEM_OK);
                for(unsigned j=0;j<v->count;++j)if(v->queued[(v->head+j)%4].sequence==b.sequence)return fail(g,c,CTR_MEM_OK);
                v->queued[(v->head+v->count)%4]=b;++v->count;add(&stats.queued,1);
            }
        }
        /* Keep the original probe's polyphase mode at unity rate. The verified
         * component profile explicitly selects linear resampling. */
        if(v->current.sequence && (!v->step || (!v->interpolation &&
           (v->step!=UINT64_C(0x100000000) || v->phase))))return fail(g,c,CTR_MEM_OK);
        if(!d->verified_component && v->current.sequence && v->format==ADPCM &&
           (v->step!=UINT64_C(0x100000000) || v->phase))
            return fail(g,c,CTR_MEM_OK);
    }
    int64_t mix[CTR_DSP_FRAME_SAMPLES*2]={0};uint8_t status[CTR_DSP_CHANNELS*12]={0},capture[CTR_DSP_FRAME_SAMPLES*4];
    int32_t aux_send[AUX_BUSES][AUX_LANES][CTR_DSP_FRAME_SAMPLES]={0};
    for(unsigned c=0;c<CTR_DSP_CHANNELS;++c) {
        voice *v=&voices[c];
        unsigned sends_aux=0;
        for(unsigned bus=0;bus<AUX_BUSES;++bus)
            for(unsigned lane=0;lane<AUX_LANES;++lane)if(v->aux_gain[bus][lane])sends_aux|=1u<<bus;
        for(unsigned i=0;i<CTR_DSP_FRAME_SAMPLES && v->playing && v->current.sequence;++i) {
            buffer *b=&v->current;int32_t s;
            if(!voice_sample(g,v,&s))return false;
            s=filter_sample(v,s);
            for(unsigned lane=0;lane<2;++lane)mix[i*2+lane]+=rounded((int64_t)s*v->gain[lane]);
            if(sends_aux)for(unsigned bus=0;bus<AUX_BUSES;++bus)if(sends_aux&(1u<<bus))
                for(unsigned lane=0;lane<AUX_LANES;++lane)
                    aux_send[bus][lane][i]+=(int32_t)rounded((int64_t)s*v->aux_gain[bus][lane]);
            uint64_t phase=v->phase+v->step;v->phase=(uint32_t)phase;
            unsigned advance=(unsigned)(phase>>32);
            while(advance-- && b->sequence)if(++b->position==b->count) {
                if(b->looping) { b->position=0;b->decoder=b->initial;add(&stats.loops,1); }
                else {
                    v->last=b->sequence;add(&stats.completed,1);
                    if(v->count) {
                        const buffer *next=&v->queued[v->head];
                        /* Faster rates can cross the boundary with unread source samples. */
                        if(next->continue_history && !adpcm_sample(g,v->coefficients,b,b->count-1u,&s))return false;
                        CtrDspAdpcmState start=adpcm_start(next,b);
                        *b=*next;b->decoder=start;v->head=(v->head+1)%4;--v->count;
                    }
                    else { *b=(buffer){0};v->playing=false;v->phase=0; }
                }
            }
        }
        uint8_t *s=status+c*12;put(s,2,0x100u+(v->playing?1u:0u));put(s+2,2,v->sync);
        put(s+4,4,rotate(v->current.position));put(s+8,2,v->current.sequence);put(s+10,2,v->last);
    }
    int16_t pcm[CTR_DSP_FRAME_SAMPLES*2];
    for(unsigned i=0;i<CTR_DSP_FRAME_SAMPLES;++i)for(unsigned lane=0;lane<2;++lane) {
        unsigned sample=i*2+lane;
        int64_t mixed=rounded(mix[sample]*master);
        for(unsigned bus=0;bus<AUX_BUSES;++bus) {
            int64_t auxiliary=(int64_t)aux_send[bus][lane][i]+aux_send[bus][lane+2u][i];
            if(aux_enabled[bus]) {
                /* The CPU can modify the previous send in the consumed bank. */
                const uint8_t *front=ram+AUXILIARY+bus*AUX_BUS_BYTES+(lane*CTR_DSP_FRAME_SAMPLES+i)*4u;
                const uint8_t *rear=front+2u*CTR_DSP_FRAME_SAMPLES*4u;
                auxiliary=signed_word(le(front,4))+signed_word(le(rear,4));
            }
            mixed+=rounded(auxiliary*aux_volume[bus]);
        }
        pcm[sample]=clipped(mixed);
        put(capture+sample*2,2,(uint16_t)pcm[sample]);
    }
    /* All command bytes, sources and output ownership validated before commit.
     * No callbacks/allocation/native AX occur within a DSP frame. */
    uint8_t *dst=g->storage[7]+offset;
    /* NDSP advances FrameId before reading vars[2]/vars[6]. DSP output belongs
     * to the opposite bank; the consumed input counter stays in its own bank. */
    uint8_t *response=g->storage[7]+((d->bank^1u)*BANK);
    memcpy(response+STATUS,status,sizeof(status));memcpy(response+CAPTURE,capture,sizeof(capture));
    for(unsigned bus=0;bus<AUX_BUSES;++bus)if(aux_enabled[bus])
        for(unsigned lane=0;lane<AUX_LANES;++lane)for(unsigned i=0;i<CTR_DSP_FRAME_SAMPLES;++i)
            put(response+AUXILIARY+bus*AUX_BUS_BYTES+(lane*CTR_DSP_FRAME_SAMPLES+i)*4u,4,(uint32_t)aux_send[bus][lane][i]);
    if(d->verified_component)memset(response+COMPONENT_DEBUG,0,COMPONENT_DEBUG_BYTES);
    for(unsigned c=0;c<CTR_DSP_CHANNELS;++c) { put(dst+CHANNEL+c*STRIDE,4,0);put(dst+CHANNEL+c*STRIDE+0x4a,2,0); }
    put(dst+MASTER,4,0);
    (void)ctr_memory_backing_changed(&g->memory,dst,FRAME_SPAN);
    (void)ctr_memory_backing_changed(&g->memory,response,span);
    pins(g,d->voices,false);pins(g,voices,true);
    memcpy(d->voices,voices,sizeof(voices));d->master=master;
    memcpy(d->aux_volume,aux_volume,sizeof(aux_volume));memcpy(d->aux_enabled,aux_enabled,sizeof(aux_enabled));
    memcpy(d->output[(d->first+d->count)%CTR_DSP_OUTPUT_FRAMES],pcm,sizeof(pcm));++d->count;
    add(&stats.frames,1);add(&stats.samples,CTR_DSP_FRAME_SAMPLES);stats.pending=false;d->stats=stats;
    d->bank^=1;d->counter=(uint16_t)(d->counter+1);if(!d->counter)d->counter=2;
    if(d->irq_live)g->events[d->irq].signaled=true;
    return true;
}
bool ctr_dsp_poll(ctr_guest *g,uint64_t now_ns) {
    if(!g || !g->dsp || !g->dsp->stats.pending || now_ns<g->dsp->deadline)return true;
    return frame(g);
}
bool ctr_dsp_next_deadline(const ctr_guest *g,uint64_t *out) {
    if(!g || !g->dsp || !g->dsp->stats.pending || !out ||
       (uintptr_t)out%_Alignof(uint64_t) || host_alias(g,out,sizeof(*out)))return false;
    *out=g->dsp->deadline;return true;
}
bool ctr_dsp_signal(ctr_guest *g,unsigned event,bool *handled) {
    struct ctr_guest_dsp *d=g->dsp;*handled=d && d->sem_live && d->sem==event;
    if(!*handled)return false;
    if(!d->stats.running || d->stats.pending)return fail(g,event,CTR_MEM_OK);
    /* Exact rational 160-sample period. Carry the fractional nanoseconds. */
    uint64_t previous=d->remainder;
    uint64_t numerator=UINT64_C(160)*512*1000000000+previous;
    uint64_t duration=numerator/16756991;d->remainder=numerator%16756991;
    if(UINT64_MAX-g->now_ns<duration+1)return fail(g,event,CTR_MEM_OVERFLOW);
    d->deadline=g->now_ns+duration+(d->remainder!=0)-(previous!=0);d->stats.pending=true;return true;
}
bool ctr_dsp_read(ctr_guest *g,int16_t *stereo,size_t capacity) {
    if(!g || !g->dsp || !g->dsp->count || !stereo || capacity<CTR_DSP_FRAME_SAMPLES*2 ||
       (uintptr_t)stereo%_Alignof(int16_t) || host_alias(g,stereo,CTR_DSP_FRAME_SAMPLES*4))return false;
    struct ctr_guest_dsp *d=g->dsp;memcpy(stereo,d->output[d->first],CTR_DSP_FRAME_SAMPLES*4);
    d->first=(d->first+1)%CTR_DSP_OUTPUT_FRAMES;--d->count;add(&d->stats.output_reads,1);return true;
}
bool ctr_dsp_stats(const ctr_guest *g,ctr_dsp_statistics *out) {
    if(!g || !out || (uintptr_t)out%_Alignof(ctr_dsp_statistics) || host_alias(g,out,sizeof(*out)))return false;
    *out=g->dsp?g->dsp->stats:(ctr_dsp_statistics){0};
    out->guest_pc=g->cpu.r[15];out->guest_ipc_header=g->ipc_header;
    out->guest_fault=(uint32_t)g->fault;out->guest_fault_detail=g->fault_detail;
    return true;
}
bool ctr_dsp_pcm48k(const int16_t *input,size_t frames,int16_t *output,size_t capacity,size_t *written) {
    if(!input || !output || !written || !frames || frames>65536 ||
       (uintptr_t)input%_Alignof(int16_t) || (uintptr_t)output%_Alignof(int16_t) ||
       (uintptr_t)written%_Alignof(size_t))return false;
    size_t count=(size_t)(((uint64_t)frames*24576000+16756990)/16756991);
    if(capacity<count || (uintptr_t)input>UINTPTR_MAX-frames*4 ||
       (uintptr_t)output>UINTPTR_MAX-count*4 ||
       overlap(input,frames*4,output,count*4) || overlap(written,sizeof(*written),input,frames*4) ||
       overlap(written,sizeof(*written),output,count*4))return false;
    for(size_t i=0;i<count;++i) {
        uint64_t phase=(uint64_t)i*16756991;size_t at=(size_t)(phase/24576000);
        uint64_t fraction=phase%24576000;size_t next=at+1<frames?at+1:at;
        for(unsigned lane=0;lane<2;++lane) {
            int64_t value=(int64_t)input[at*2+lane]*(int64_t)(24576000-fraction)+
                          (int64_t)input[next*2+lane]*(int64_t)fraction+12288000;
            value=value>=0?value/24576000:-((-value+24575999)/24576000);
            output[i*2+lane]=(int16_t)value;
        }
    }
    *written=count;return true;
}
static bool component_matches(ctr_guest *g,uint32_t address,uint32_t size) {
    if(size==sizeof(ctr_dsp_component)) {
        uint8_t token[sizeof(ctr_dsp_component)];
        return range(g,address,size,CTR_MEM_READ) &&
               ctr_memory_read_bytes(&g->memory,address,token,size)==CTR_MEM_OK &&
               !memcmp(token,ctr_dsp_component,size);
    }
    const ctr_dsp_firmware *firmware=g->dsp_firmware;
    if(!firmware || size!=firmware->size || size<0x300u || size>CTR_DSP_RAM_BYTES ||
       !range(g,address,size,CTR_MEM_READ))return false;
    ctr_sha256 hash;ctr_sha256_init(&hash);
    uint8_t block[1024],digest[32];
    for(uint32_t offset=0;offset<size;) {
        uint32_t count=size-offset;
        if(count>sizeof(block))count=sizeof(block);
        if(ctr_memory_read_bytes(&g->memory,address+offset,block,count)!=CTR_MEM_OK ||
           !ctr_sha256_update(&hash,block,count))return false;
        offset+=count;
    }
    return ctr_sha256_final(&hash,digest) && !memcmp(digest,firmware->sha256,sizeof(digest));
}
bool ctr_dsp_request(ctr_guest *g,unsigned session,const uint32_t *w,uint32_t command,uint32_t *out,unsigned *count) {
    unsigned id=w[0]>>16;out[0]=(id<<16)|0x40;out[1]=0;*count=2;
    struct ctr_guest_dsp *d=g->dsp;
    if(w[0]==0x001100c2u) {
        if(w[1]>CTR_DSP_RAM_BYTES || (uint16_t)w[2]!=0xff || (uint16_t)w[3]!=0xff ||
           w[4]!=(w[1]<<4|0xau) || !component_matches(g,w[5],w[1]))return false;
        if(d && d->stats.loaded) { out[1]=BUSY;return true; }
        if(!d) { g->dsp=calloc(1,sizeof(*d));d=g->dsp;if(!d) { out[1]=NO_MEMORY;return true; } }
        if(!g->storage[CTR_DSP_STORAGE_INDEX]) {
            /* The DSP has its own fixed RAM outside the application's FCRAM budget. */
            uint8_t *ram=calloc(1,CTR_DSP_RAM_BYTES);
            if(!ram) { out[1]=NO_MEMORY;return true; }
            if(ctr_memory_map(&g->memory,CTR_DSP_RAM,CTR_DSP_RAM_BYTES,3,ram,CTR_DSP_RAM_BYTES)!=CTR_MEM_OK) { free(ram);out[1]=NO_MEMORY;return true; }
            g->storage[CTR_DSP_STORAGE_INDEX]=ram;g->storage_size[CTR_DSP_STORAGE_INDEX]=CTR_DSP_RAM_BYTES;
        }
        memset(g->storage[7],0,CTR_DSP_RAM_BYTES);ctr_memory_backing_changed(&g->memory,g->storage[7],CTR_DSP_RAM_BYTES);
        d->stats.loaded=true;d->owner=session;d->master=32768;d->counter=4;
        d->verified_component=w[1]!=sizeof(ctr_dsp_component);
        out[0]=(id<<16)|0x82;out[2]=1;out[3]=w[4];out[4]=w[5];*count=5;return true;
    }
    if(w[0]==0x00120000u) { if(d && d->stats.loaded && d->owner!=session) { out[1]=INVALID;return true; }reset(g);return true; }
    if(w[0]==0x001f0000u) { out[0]=(id<<16)|0x80u;out[2]=g->dsp_headphones;*count=3;return true; }
    if(!d || !d->stats.loaded || d->owner!=session) { out[1]=INVALID;return true; }
    switch(w[0]) {
        case 0x000c0040:
            if(w[1]>=0x20000)return false;
            out[0]=(id<<16)|0x80;out[2]=CTR_DSP_RAM+w[1]*2;*count=3;return true;
        case 0x00150082: {
            if(w[1]!=2 || w[2]!=2 || w[3])return false;
            ctr_guest_handle *h=w[4]?ctr_guest_handle_for(g,w[4]):NULL;
            if(w[4] && (!h || h->type!=CTR_HANDLE_EVENT)) { out[1]=INVALID;return true; }
            if(h)ctr_guest_object_retain(g,CTR_HANDLE_EVENT,h->object);
            if(d->irq_live)ctr_guest_object_release(g,CTR_HANDLE_EVENT,d->irq);
            d->irq_live=h!=NULL;if(h)d->irq=h->object;return true;
        }
        case 0x00160000: {
            uint32_t h;
            if(!d->sem_live) {
                h=ctr_guest_event_create(g,1);if(!h) { out[1]=NO_MEMORY;return true; }
                d->sem=ctr_guest_handle_for(g,h)->object;d->sem_live=true;
                ctr_guest_object_retain(g,CTR_HANDLE_EVENT,d->sem);
            } else h=ctr_guest_handle_new(g,CTR_HANDLE_EVENT,d->sem);
            if(!h) { out[1]=NO_MEMORY;return true; }
            out[0]|=2;out[2]=0;out[3]=h;*count=4;return true;
        }
        case 0x00170040: if(w[1]!=0x2000)return false;d->mask=(uint16_t)w[1];return true;
        case 0x00070040: return w[1]==0x4000;
        case 0x000d0082: {
            if(w[1]!=2 || w[2]!=4 || w[3]!=(4u<<14|1u<<10|2u))return false;
            if(!range(g,w[4],4,CTR_MEM_READ))return false;
            uint8_t payload[4];ctr_memory_read_bytes(&g->memory,w[4],payload,4);
            uint32_t value=le(payload,4);
            if(value>1)return false;
            if(!value) {
                if(d->stats.running || !d->irq_live || !d->sem_live || d->mask!=0x2000)return false;
                d->pipe_size=2+sizeof(variables);d->pipe_at=0;
                for(unsigned i=0;i<sizeof(variables)/2;++i)put(d->pipe+2+i*2,2,variables[i]);
                if(d->verified_component) {
                    /* The retail audio worker reads the 0x260-byte DSP debug
                       block. Reserve it beyond the intermediate mix and compressor. */
                    put(d->pipe+d->pipe_size-2,2,COMPONENT_COMPRESSOR/2);
                    put(d->pipe+d->pipe_size,2,COMPONENT_DEBUG/2);
                    d->pipe_size+=2;
                }
                put(d->pipe,2,(d->pipe_size-2)/2);
                d->stats.running=true;g->events[d->irq].signaled=true;
            } else {
                d->stats.running=d->stats.pending=false;d->reply_ready=true;
                pins(g,d->voices,false);memset(d->voices,0,sizeof(d->voices));
                memset(d->aux_enabled,0,sizeof(d->aux_enabled));memset(d->aux_volume,0,sizeof(d->aux_volume));
            }
            return true;
        }
        case 0x001000c0: {
            unsigned size=(uint16_t)w[3];
            if(w[1]!=2 || w[2] || !size || size>sizeof(d->pipe))return false;
            uint32_t desc,address;
            if((uint64_t)g->cpu.tls_address+0x188>UINT64_C(0x100000000))return fail(g,g->cpu.tls_address,CTR_MEM_OVERFLOW);
            if(!range(g,g->cpu.tls_address+0x180,8,CTR_MEM_READ))return false;
            ctr_memory_read(&g->memory,g->cpu.tls_address+0x180,4,CTR_MEM_READ,&desc);
            ctr_memory_read(&g->memory,g->cpu.tls_address+0x184,4,CTR_MEM_READ,&address);
            if(desc!=(size<<14|2u))return false;
            if(!range(g,address,size,CTR_MEM_WRITE))return false;
            if(guest_alias(g,address,size,command,0x108) || guest_alias(g,address,size,CTR_DSP_RAM,CTR_DSP_RAM_BYTES))return false;
            unsigned n=d->pipe_size-d->pipe_at>=size?size:0;
            if(n)ctr_memory_write_bytes(&g->memory,address,d->pipe+d->pipe_at,n);
            d->pipe_at+=n;out[0]=(id<<16)|0x82;out[2]=n;out[3]=desc;out[4]=address;*count=5;return true;
        }
        case 0x00010040: case 0x00020040:
            if(w[1])return false;
            if(w[0]==0x00010040 && !d->reply_ready)return false;
            out[0]=(id<<16)|0x80;out[2]=d->reply_ready;*count=3;
            if(w[0]==0x00010040)d->reply_ready=false;
            return true;
        case 0x00130082: case 0x00140082:
            if(w[3] || w[4]!=0xffff8001u || !w[2] || w[2]>CTR_GUEST_MAX_BYTES)return false;
            if(!range(g,w[1],w[2],CTR_MEM_READ))return false;
            ctr_cpu_barrier();return true;
        default:return false;
    }
}
