#include "ctr_audio_fixture.h"
#include <string.h>

#define BLOCK_SAMPLES 1792u
#define BLOCK_BYTES 1024u
#define BLOCKS 27u
#define LAST_SAMPLES (CTR_AUDIO_FIXTURE_SAMPLES-(BLOCKS-1u)*BLOCK_SAMPLES)
#define LAST_BYTES ((LAST_SAMPLES/14u)*8u+1u+(LAST_SAMPLES%14u+1u)/2u)
#define LAST_STRIDE ((LAST_BYTES+3u)/4u*4u)
static void w16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void w32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));
}
static void ref(uint8_t *d,size_t at,uint16_t type,size_t base,size_t dest) {
    w16(d+at,type);w32(d+at+4,(uint32_t)(dest-base));
}
static int nibble(unsigned sample,unsigned channel) {
    static const unsigned frequencies[2][3]={{220,330,440},{660,550,440}};
    unsigned phrase=sample/16000u,within=sample%16000u;
    if(within>=12800u)return 0;
    unsigned phase=within*frequencies[channel][phrase]%CTR_AUDIO_FIXTURE_RATE;
    /* Symmetric triangle in [-6,6]. The integer 128-sample attack/release
     * prevents a large edge at phrase starts/stops; output is quantized PCM. */
    int wave=phase<16000u?(int)(phase*24u/32000u)-6:18-(int)(phase*24u/32000u);
    unsigned envelope=within<128u?within:12800u-within<128u?12800u-within:128u;
    return wave*(int)envelope/128;
}
static bool build(CtrAudioFixture *fixture,unsigned channels,uint32_t rate,unsigned motif) {
    if(!fixture || channels<1 || channels>2 || rate<8000 || rate>96000 || motif>1)return false;
    const size_t info=64,info_bytes=512,seek=info+info_bytes;
    size_t seek_bytes=(8u+BLOCKS*channels*4u+31u)/32u*32u;
    size_t audio=seek+seek_bytes,start=audio+32;
    size_t audio_bytes=(32u+((BLOCKS-1u)*BLOCK_BYTES+LAST_STRIDE)*channels+31u)/32u*32u;
    size_t bytes=audio+audio_bytes;
    if(bytes>sizeof(fixture->data))return false;
    uint8_t *d=fixture->data;memset(d,0,sizeof(fixture->data));
    memcpy(d,"CSTM",4);w16(d+4,0xfeff);w16(d+6,64);w32(d+8,0x02000000);
    w32(d+12,(uint32_t)bytes);w16(d+16,3);
    const size_t positions[3]={info,seek,audio},sizes[3]={info_bytes,seek_bytes,audio_bytes};
    const char *magic[3]={"INFO","SEEK","DATA"};
    for(unsigned i=0;i<3;++i) {
        w16(d+20+i*12,(uint16_t)(0x4000+i));w32(d+24+i*12,(uint32_t)positions[i]);
        w32(d+28+i*12,(uint32_t)sizes[i]);memcpy(d+positions[i],magic[i],4);
        w32(d+positions[i]+4,(uint32_t)sizes[i]);
    }
    size_t stream=info+64,table=info+160;
    ref(d,info+8,0x4100,info+8,stream);w32(d+info+20,UINT32_MAX);
    ref(d,info+24,0x0101,info+8,table);w32(d+table,channels);
    d[stream]=2;d[stream+2]=(uint8_t)channels;w32(d+stream+4,rate);
    w32(d+stream+12,CTR_AUDIO_FIXTURE_SAMPLES);w32(d+stream+16,BLOCKS);
    w32(d+stream+20,BLOCK_BYTES);w32(d+stream+24,BLOCK_SAMPLES);
    w32(d+stream+28,LAST_BYTES);w32(d+stream+32,LAST_SAMPLES);
    w32(d+stream+36,LAST_STRIDE);w32(d+stream+40,4);w32(d+stream+44,BLOCK_SAMPLES);
    ref(d,stream+48,0x1f00,audio+8,start);
    for(unsigned c=0;c<channels;++c) {
        size_t record=info+192+c*16,adpcm=info+256+c*64;
        ref(d,table+4+c*8,0x4102,table,record);ref(d,record,0x0300,record,adpcm);
        d[adpcm+32]=10;
        int16_t h1=0,h2=0;
        for(unsigned b=0;b<BLOCKS;++b) {
            size_t history=seek+8+(b*channels+c)*4;
            w16(d+history,(uint16_t)h1);w16(d+history+2,(uint16_t)h2);
            unsigned count=b+1==BLOCKS?LAST_SAMPLES:BLOCK_SAMPLES;
            size_t stride=b+1==BLOCKS?LAST_STRIDE:BLOCK_BYTES;
            size_t at=start+(size_t)b*BLOCK_BYTES*channels+c*stride;
            for(unsigned n=0;n<count;++n) {
                unsigned frame=n/14,lane=n%14;int value=nibble(b*BLOCK_SAMPLES+n,channels==1?motif:c);
                if(!lane)d[at+frame*8]=10;
                unsigned shift=lane%2?0:4;
                d[at+frame*8+1+lane/2]|=(uint8_t)(((unsigned)value&15u)<<shift);
                h2=h1;h1=(int16_t)(value*1024);
            }
        }
    }
    fixture->bytes=bytes;return true;
}
bool ctr_audio_fixture_build_rate(CtrAudioFixture *fixture,unsigned channels,uint32_t rate) {
    return build(fixture,channels,rate,0);
}
bool ctr_audio_fixture_build(CtrAudioFixture *fixture,unsigned channels) {
    return ctr_audio_fixture_build_rate(fixture,channels,CTR_AUDIO_FIXTURE_RATE);
}
bool ctr_audio_fixture_build_mono(CtrAudioFixture *fixture,uint32_t rate,unsigned motif) {
    return build(fixture,1,rate,motif);
}
