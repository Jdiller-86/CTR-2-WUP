#include "ctr_cstm.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    CtrCstmInfo info;
    size_t seek, data, channel[2];
} Parsed;
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|(uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int16_t s16(const uint8_t *p) {
    int32_t v=u16(p);return (int16_t)(v>=32768?v-65536:v);
}
static bool range(size_t at,size_t count,size_t end) { return at<=end && count<=end-at; }
static bool separate(size_t a,size_t an,size_t b,size_t bn) { return a+an<=b || b+bn<=a; }
static CtrCstmResult arguments(const uint8_t *data,size_t bytes,void *out,size_t count) {
    if(!data || !out)return CTR_CSTM_ARGUMENT;
    if(bytes>CTR_CSTM_MAX_BYTES || bytes>UINTPTR_MAX-(uintptr_t)data ||
       count>UINTPTR_MAX-(uintptr_t)out)return CTR_CSTM_BOUNDS;
    if(!separate((uintptr_t)data,bytes,(uintptr_t)out,count))return CTR_CSTM_OVERLAP;
    return CTR_CSTM_OK;
}
/* Reference offsets are relative to their specified parent, never the ref itself
 * unless that parent is the channel record. All destinations stay inside INFO. */
static bool ref(const uint8_t *d,size_t at,uint16_t type,size_t base,
                size_t lower,size_t end,size_t count,size_t *dest) {
    if(!range(at,8,end) || u16(d+at)!=type)return false;
    uint32_t offset=u32(d+at+4);
    if(offset==UINT32_MAX || offset>end || base>end-offset)return false;
    *dest=base+offset;
    return *dest>=lower && range(*dest,count,end);
}
static size_t needed(uint32_t samples) {
    return (size_t)(samples/14)*8+(samples%14?1+(samples%14+1)/2:0);
}
static CtrCstmResult parse(const uint8_t *d,size_t bytes,Parsed *p) {
    if(bytes<64)return CTR_CSTM_BOUNDS;
    if(memcmp(d,"CSTM",4))return CTR_CSTM_FORMAT;
    if(u16(d+4)!=0xfeff || u32(d+8)!=0x02000000)return CTR_CSTM_UNSUPPORTED;
    if(u16(d+6)!=64 || u32(d+12)!=bytes || u16(d+16)!=3)return CTR_CSTM_FORMAT;
    size_t section[3]={0},length[3]={0};
    const char *magic[3]={"INFO","SEEK","DATA"};
    for(unsigned i=0;i<3;++i) {
        size_t at=20+i*12;unsigned type=u16(d+at);
        if(type<0x4000 || type>0x4002)return CTR_CSTM_UNSUPPORTED;
        unsigned j=type-0x4000;
        size_t pos=u32(d+at+4),n=u32(d+at+8);
        if(section[j] || pos<64 || pos%32 || n<8 || !range(pos,n,bytes))return CTR_CSTM_FORMAT;
        if(memcmp(d+pos,magic[j],4) || u32(d+pos+4)!=n)return CTR_CSTM_FORMAT;
        section[j]=pos;length[j]=n;
    }
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)
        if(!separate(section[i],length[i],section[j],length[j]))return CTR_CSTM_FORMAT;
    size_t info=section[0],end=info+length[0],s,channels;
    if(length[0]<32 || !ref(d,info+8,0x4100,info+8,info+32,end,56,&s) ||
       !ref(d,info+24,0x0101,info+8,info+32,end,4,&channels))return CTR_CSTM_FORMAT;
    if(d[s]!=2 || d[s+1]!=0 || d[s+2]<1 || d[s+2]>2)return CTR_CSTM_UNSUPPORTED;
    CtrCstmInfo *v=&p->info;
    v->channels=d[s+2];v->sample_rate=u32(d+s+4);v->samples=u32(d+s+12);
    v->blocks=u32(d+s+16);v->block_bytes=u32(d+s+20);v->block_samples=u32(d+s+24);
    v->last_bytes=u32(d+s+28);v->last_samples=u32(d+s+32);v->last_padded_bytes=u32(d+s+36);
    if(!v->sample_rate || v->sample_rate>384000 || u32(d+s+8)!=0 ||
       !v->blocks || v->blocks>CTR_CSTM_MAX_BLOCKS || !v->block_samples ||
       v->block_samples>CTR_DSP_ADPCM_MAX_SAMPLES || v->block_samples%14 ||
       v->block_bytes!=needed(v->block_samples) || !v->last_samples ||
       v->last_samples>v->block_samples || v->last_bytes<needed(v->last_samples) ||
       v->last_bytes>(v->last_samples+13)/14*8 || v->last_padded_bytes<v->last_bytes ||
       v->last_padded_bytes>v->block_bytes ||
       (uint64_t)(v->blocks-1)*v->block_samples+v->last_samples!=v->samples)return CTR_CSTM_FORMAT;
    if(u32(d+s+40)!=4 || u32(d+s+44)!=v->block_samples)return CTR_CSTM_UNSUPPORTED;
    /* The reference itself is in INFO; its destination is in DATA. These may
     * appear in either order, so they have distinct bounds. */
    uint32_t relative=u32(d+s+52);
    if(u16(d+s+48)!=0x1f00 || relative>length[2]-8)return CTR_CSTM_FORMAT;
    size_t data_offset=section[2]+8+relative;
    if(data_offset%32)return CTR_CSTM_FORMAT;
    uint64_t payload=((uint64_t)(v->blocks-1)*v->block_bytes+v->last_padded_bytes)*v->channels;
    if(payload>section[2]+length[2]-data_offset ||
       (uint64_t)v->blocks*v->channels*4>length[1]-8)return CTR_CSTM_BOUNDS;
    p->data=data_offset;p->seek=section[1]+8;
    if(u32(d+channels)!=v->channels || !range(channels+4,v->channels*8,end))return CTR_CSTM_FORMAT;
    for(unsigned c=0;c<v->channels;++c) {
        size_t channel,adpcm;
        if(!ref(d,channels+4+c*8,0x4102,channels,info+32,end,8,&channel) ||
           !ref(d,channel,0x0300,channel,info+32,end,46,&adpcm))return CTR_CSTM_FORMAT;
        if(d[adpcm+32]>0x7f || u16(d+adpcm+34)!=u16(d+p->seek+c*4) ||
           u16(d+adpcm+36)!=u16(d+p->seek+c*4+2))return CTR_CSTM_FORMAT;
        p->channel[c]=adpcm;
    }
    /* Bounds-check optional tracks but leave volume/pan/mixing to a later layer. */
    size_t tracks;
    if(u16(d+info+16)==0 && u32(d+info+20)==UINT32_MAX)return CTR_CSTM_OK;
    if(!ref(d,info+16,0x0101,info+8,info+32,end,4,&tracks))return CTR_CSTM_FORMAT;
    uint32_t count=u32(d+tracks);
    if(count>2)return CTR_CSTM_UNSUPPORTED;
    if(!range(tracks+4,count*8,end))return CTR_CSTM_FORMAT;
    for(uint32_t t=0;t<count;++t) {
        size_t track,indices;
        if(!ref(d,tracks+4+t*8,0x4101,tracks,info+32,end,12,&track) ||
           !ref(d,track+4,0x0100,track,info+32,end,4,&indices))return CTR_CSTM_FORMAT;
        uint32_t n=u32(d+indices);
        if(!n || n>v->channels || !range(indices+4,n,end))return CTR_CSTM_FORMAT;
        for(uint32_t c=0;c<n;++c)if(d[indices+4+c]>=v->channels)return CTR_CSTM_FORMAT;
    }
    return CTR_CSTM_OK;
}
CtrCstmResult ctr_cstm_info(const uint8_t *data,size_t bytes,CtrCstmInfo *output) {
    CtrCstmResult r=arguments(data,bytes,output,sizeof(*output));
    if(r!=CTR_CSTM_OK)return r;
    Parsed p={0};r=parse(data,bytes,&p);
    if(r==CTR_CSTM_OK)*output=p.info;
    return r;
}
CtrCstmResult ctr_cstm_block(const uint8_t *data,size_t bytes,
    uint32_t block,unsigned channel,CtrCstmBlock *output) {
    CtrCstmResult r=arguments(data,bytes,output,sizeof(*output));
    if(r!=CTR_CSTM_OK)return r;
    Parsed p={0};r=parse(data,bytes,&p);
    if(r!=CTR_CSTM_OK)return r;
    if(block>=p.info.blocks || channel>=p.info.channels)return CTR_CSTM_BOUNDS;
    CtrCstmBlock v={0};bool last=block==p.info.blocks-1;
    size_t stride=last?p.info.last_padded_bytes:p.info.block_bytes;
    v.encoded=data+p.data+(size_t)block*p.info.block_bytes*p.info.channels+channel*stride;
    v.bytes=last?p.info.last_bytes:p.info.block_bytes;
    v.first_sample=block*p.info.block_samples;
    v.samples=last?p.info.last_samples:p.info.block_samples;
    size_t history=p.seek+((size_t)block*p.info.channels+channel)*4;
    v.state.history1=s16(data+history);v.state.history2=s16(data+history+2);
    for(unsigned c=0;c<16;++c)v.coefficients[c]=s16(data+p.channel[channel]+c*2);
    *output=v;return CTR_CSTM_OK;
}
const char *ctr_cstm_result_name(CtrCstmResult result) {
    switch(result) {
    case CTR_CSTM_OK:return "ok";
    case CTR_CSTM_ARGUMENT:return "argument";
    case CTR_CSTM_BOUNDS:return "bounds";
    case CTR_CSTM_FORMAT:return "format";
    case CTR_CSTM_UNSUPPORTED:return "unsupported";
    case CTR_CSTM_OVERLAP:return "overlap";
    default:return "invalid";
    }
}
