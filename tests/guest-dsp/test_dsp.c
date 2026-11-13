#include "ctr_dsp_fixture.h"
#include "ctr_dsp_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static int16_t pcm[96000],other[96000];
static ctr_guest *opened(void) {
    ctr_guest *g=calloc(1,sizeof(*g));CHECK(g && ctr_guest_open(g,&ctr_dsp_image,ctr_dsp_fixture_blocks,ctr_dsp_fixture_block_count));
    CHECK(ctr_guest_run(g,1000000,0,false)==CTR_GUEST_WAITING);return g;
}
static void closed(ctr_guest *g) { ctr_guest_close(g);free(g); }
static void write_at(ctr_guest *g,uint32_t address,uint32_t value) { CHECK(ctr_memory_write(&g->memory,address,4,value)==CTR_MEM_OK); }
static uint32_t read_at(ctr_guest *g,uint32_t address) { uint32_t v;CHECK(ctr_memory_read(&g->memory,address,4,CTR_MEM_READ,&v)==CTR_MEM_OK);return v; }
static void exact_deadlines_and_ipc(void) {
    ctr_guest *g=opened();ctr_dsp_statistics stats;
    for(unsigned i=1;i<=6;++i) {
        uint64_t deadline=((uint64_t)i*160*512*1000000000+16756990)/16756991;
        CHECK(ctr_guest_run(g,1000000,deadline-1,false)==CTR_GUEST_WAITING);
        CHECK(ctr_dsp_stats(g,&stats) && stats.frames==i-1);
        CHECK(ctr_guest_run(g,1000000,deadline,false)==CTR_GUEST_WAITING);
        CHECK(ctr_dsp_stats(g,&stats) && stats.frames==i && ctr_dsp_read(g,other,320));
    }
    closed(g);
    g=opened();uint32_t service=g->cpu.r[5],base=g->cpu.tls_address+0x80;
    // Standard word-address conversion and explicit mapped-buffer reply shape.
    write_at(g,base,0x000c0040);write_at(g,base+4,0x10080);g->cpu.r[0]=service;ctr_guest_svc(g,0x32);
    CHECK(read_at(g,base)==0x000c0080 && read_at(g,base+8)==CTR_DSP_RAM+0x20100);
    // Read the pipe table through the thread's static output descriptor.
    write_at(g,g->cpu.tls_address+0x180,0x8002);write_at(g,g->cpu.tls_address+0x184,0x08000080);
    write_at(g,base,0x001000c0);write_at(g,base+4,2);write_at(g,base+8,0);write_at(g,base+12,2);
    g->cpu.r[0]=service;ctr_guest_svc(g,0x32);
    CHECK(read_at(g,base)==0x00100082 && read_at(g,base+8)==2 && (read_at(g,0x08000080)&65535)==9);
    write_at(g,g->cpu.tls_address+0x180,(18u<<14)|2);
    write_at(g,base,0x001000c0);write_at(g,base+4,2);write_at(g,base+8,0);write_at(g,base+12,18);
    g->cpu.r[0]=service;ctr_guest_svc(g,0x32);CHECK(read_at(g,base+8)==18);
    const unsigned spans[]={2,24*192,24*12,24*32,0xc4,0x1080,160*4,160*4*4,160*4*4};
    uint32_t addresses[9];
    for(unsigned i=0;i<9;++i) {
        uint32_t value;CHECK(ctr_memory_read(&g->memory,0x08000080+i*2,2,CTR_MEM_READ,&value)==CTR_MEM_OK);
        addresses[i]=value*2;CHECK(addresses[i]+spans[i]<=0x20000);
        for(unsigned j=0;j<i;++j)CHECK(addresses[i]>=addresses[j]+spans[j] || addresses[j]>=addresses[i]+spans[i]);
    }
    write_at(g,g->cpu.tls_address+0x180,0x8002);
    // The same output may not overwrite IPC through a backing alias.
    write_at(g,g->cpu.tls_address+0x184,base);
    write_at(g,base,0x001000c0);write_at(g,base+4,2);write_at(g,base+8,0);write_at(g,base+12,2);
    uint32_t before=read_at(g,base);g->cpu.r[0]=service;ctr_guest_svc(g,0x32);
    CHECK(g->state==CTR_GUEST_FAULTED && read_at(g,base)==before);closed(g);
}
static void controls_and_queue(void) {
    ctr_guest *g=opened();ctr_dsp_statistics s;
    CHECK(ctr_dsp_poll(g,4000000));CHECK(ctr_dsp_stats(g,&s) && s.frames==0 && s.pending);
    /* Pause before the first pull. Resume keeps the unconsumed source cursor. */
    write_at(g,CTR_DSP_RAM+0x100+0xa0,0x10000);
    CHECK(ctr_dsp_poll(g,5000000) && ctr_dsp_read(g,other,320));
    for(unsigned i=0;i<320;++i)CHECK(other[i]==0);
    CHECK(ctr_guest_run(g,1000000,5000000,false)==CTR_GUEST_WAITING);
    write_at(g,CTR_DSP_RAM+0x20000+0x100,0x10000);
    write_at(g,CTR_DSP_RAM+0x20000+0x100+0xa0,1);
    CHECK(ctr_dsp_poll(g,10000000) && ctr_dsp_read(g,other,320));CHECK(other[0]==6000 && other[1]==0);
    CHECK(!ctr_dsp_read(g,other,320));closed(g);
    g=opened();uint32_t base=CTR_DSP_RAM+0x100;
    write_at(g,base,read_at(g,base)|0x80000);
    write_at(g,base+0xb0,4u<<16);write_at(g,base+0xbc,1u<<16); // nonloop initial
    CHECK(ctr_memory_write(&g->memory,base+0x4a,2,15)==CTR_MEM_OK);
    for(unsigned i=0;i<4;++i) {
        uint32_t b=base+0x4c+i*20;
        write_at(g,b,0x2000);write_at(g,b+4,4u<<16);write_at(g,b+16,i+2);
    }
    CHECK(ctr_dsp_poll(g,5000000) && ctr_dsp_read(g,other,320));
    CHECK(ctr_dsp_stats(g,&s) && s.queued==6 && s.completed==5);
    for(unsigned i=0;i<160;++i)CHECK(other[i*2]==(i<20?(i%4<2?6000:-6000):0));
    CHECK(read_at(g,CTR_DSP_RAM+0x20000+0x2000+8)==(5u<<16));closed(g);
}
static void variable_rates(void) {
    static const struct { uint32_t bits,mode;unsigned count;int16_t expected[8]; } cases[]={
        {0x3f000000,1,8,{-3,-2,-1,0,1,2,3,0}}, /* Half rate, linear; loop seam. */
        {0x3f000000,2,8,{-3,-3,-1,-1,1,1,3,3}}, /* Half rate, direct sampling. */
        {0x3fc00000,1,8,{-3,0,3,-2,1,0,-1,2}}, /* 1.5x, alternating fractions. */
        {0x40000000,2,2,{-3,1}},
        {0x40800000,1,1,{-3}}
    };
    for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k) {
        ctr_guest *g=opened();uint32_t base=CTR_DSP_RAM+0x100;
        write_at(g,base+0x34,cases[k].bits);write_at(g,base+0x38,cases[k].mode);
        write_at(g,base+0xb0,4u<<16);
        for(unsigned i=0;i<4;++i)CHECK(ctr_memory_write(&g->memory,0x14000000+i*2,2,(uint16_t)(i*2-3))==CTR_MEM_OK);
        CHECK(ctr_dsp_poll(g,5000000) && ctr_dsp_read(g,other,320));
        for(unsigned i=0;i<160;++i)CHECK(other[i*2]==cases[k].expected[i%cases[k].count] && other[i*2+1]==0);
        closed(g);
    }
    /* A non-dyadic ratio carries its fractional cursor into the next DSP
     * frame. The binary32 value is just above 1/3, so these indices are exact. */
    ctr_guest *g=opened();uint32_t base=CTR_DSP_RAM+0x100;
    write_at(g,base+0x34,0x3eaaaaab);write_at(g,base+0x38,2);write_at(g,base+0xb0,4u<<16);
    for(unsigned i=0;i<4;++i)CHECK(ctr_memory_write(&g->memory,0x14000000+i*2,2,(uint16_t)(i*2-3))==CTR_MEM_OK);
    for(unsigned frame=0;frame<2;++frame) {
        CHECK(ctr_dsp_poll(g,(frame+1)*5000000u) && ctr_dsp_read(g,other,320));
        for(unsigned i=0;i<160;++i)CHECK(other[i*2]==(int)((((frame*160+i)/3)%4)*2)-3);
        if(!frame)CHECK(ctr_guest_run(g,1000000,5000000,false)==CTR_GUEST_WAITING);
    }
    closed(g);
    /* Interpolation crosses queued buffers, then holds the final sample only
     * for its fractional tail. Completed sources release their allocation pins. */
    g=opened();base=CTR_DSP_RAM+0x100;
    write_at(g,base,read_at(g,base)|0x80000);
    write_at(g,base+0x34,0x3f000000);write_at(g,base+0x38,1);
    write_at(g,base+0xb0,1u<<16);write_at(g,base+0xbc,1u<<16);
    CHECK(ctr_memory_write(&g->memory,base+0x4a,2,1)==CTR_MEM_OK);
    write_at(g,base+0x4c,0x00022000u); // physical 0x20000002, halfword-rotated
    write_at(g,base+0x50,1u<<16);write_at(g,base+0x5c,3);
    CHECK(ctr_memory_write(&g->memory,0x14000000,2,65535)==CTR_MEM_OK);
    CHECK(ctr_memory_write(&g->memory,0x14000002,2,0)==CTR_MEM_OK);
    CHECK(ctr_dsp_poll(g,5000000) && ctr_dsp_read(g,other,320));
    CHECK(other[0]==-1 && other[2]==0); // Negative half tie rounds positive.
    for(unsigned i=2;i<160;++i)CHECK(other[i*2]==0);
    CHECK(g->allocations[0].pins==1);closed(g); // Other original voice still owns its source.
}
static uint32_t adpcm_voice(ctr_guest *g) {
    static const uint8_t encoded[]={0,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
                                   1,0x11,0x11,0x11,0x11,0x11,0x11,0x11};
    uint32_t base=CTR_DSP_RAM+0x100;
    write_at(g,base,read_at(g,base)|4);write_at(g,base+0xb0,28u<<16);
    write_at(g,base+0xb4,9);write_at(g,base+0xb8,10);write_at(g,base+0xbc,(1u<<16)|3);
    write_at(g,CTR_DSP_RAM+0x2200,2048); // predictor = previous sample
    CHECK(ctr_memory_write_bytes(&g->memory,0x14000000,encoded,sizeof(encoded))==CTR_MEM_OK);
    return base;
}
static void guest_adpcm(void) {
    ctr_guest *g=opened();adpcm_voice(g);
    for(unsigned frame=0;frame<2;++frame) {
        CHECK(ctr_dsp_poll(g,(frame+1)*5000000u) && ctr_dsp_read(g,other,320));
        for(unsigned i=0;i<160;++i) {
            unsigned position=(frame*160+i)%28;
            CHECK(other[i*2]==(int)(position<14?11+position:26+(position-14)*2) && other[i*2+1]==0);
        }
        if(!frame)CHECK(ctr_guest_run(g,1000000,5000000,false)==CTR_GUEST_WAITING);
    }
    closed(g);
    /* Each queued buffer supplies its own history; completion drops its pin. */
    g=opened();uint32_t base=adpcm_voice(g);
    write_at(g,base,read_at(g,base)|0x80000);write_at(g,base+0xb0,14u<<16);
    write_at(g,base+0xbc,(1u<<16)|1);
    CHECK(ctr_memory_write(&g->memory,base+0x4a,2,1)==CTR_MEM_OK);
    uint32_t queued=base+0x4c;
    write_at(g,queued,0x00082000);write_at(g,queued+4,14u<<16);
    write_at(g,queued+8,0xfff60001); // header 1, history -10
    write_at(g,queued+12,0x10000);write_at(g,queued+16,3);
    CHECK(ctr_dsp_poll(g,5000000) && ctr_dsp_read(g,other,320));
    for(unsigned i=0;i<160;++i)CHECK(other[i*2]==(i<14?(int)i+11:i<28?((int)i-14)*2-8:0));
    CHECK(g->allocations[0].pins==1);closed(g);
    /* A malformed later ADPCM header cannot publish a partial mixed frame. */
    for(unsigned kind=0;kind<4;++kind) {
        g=opened();base=adpcm_voice(g);
        if(kind==0)CHECK(ctr_memory_write(&g->memory,0x14000008,1,0x80)==CTR_MEM_OK);
        if(kind==1)write_at(g,base+0xbc,(1u<<16)|2); // missing explicit history
        if(kind==2)write_at(g,base+0xb4,0x10009); // index differs from source header
        if(kind==3) { write_at(g,base+0x34,0x3f000000);write_at(g,base+0x38,1); }
        uint8_t before[CTR_DSP_RAM_BYTES];memcpy(before,g->storage[7],sizeof(before));
        CHECK(!ctr_dsp_poll(g,5000000) && g->state==CTR_GUEST_FAULTED);
        CHECK(!memcmp(before,g->storage[7],sizeof(before)) && !ctr_dsp_read(g,other,320));
        CHECK(g->allocations[0].pins==0);closed(g);
    }
}
static void atomic_failures(void) {
    for(unsigned kind=0;kind<14;++kind) {
        ctr_guest *g=opened();
        if(kind==0)write_at(g,CTR_DSP_RAM+0x100+23*0xc0,0x80000000); // late channel unsupported
        if(kind==1)write_at(g,CTR_DSP_RAM+0x100+0x34,0x7fc00000); // NaN rate
        if(kind==2)write_at(g,CTR_DSP_RAM+0x100+4,0x7f800000); // infinite gain
        if(kind==3)write_at(g,CTR_DSP_RAM+0x100+0xb0,0); // empty buffer
        if(kind==4)write_at(g,CTR_DSP_RAM+0x100+0xb4,10); // unsupported stereo ADPCM, never silently PCM
        if(kind==5)write_at(g,CTR_DSP_RAM+0x100+0xac,0x2004); // unowned physical
        if(kind==6)CHECK(ctr_memory_change_range(&g->memory,0x14000000,4096,0,false)==CTR_MEM_OK);
        if(kind==7)write_at(g,CTR_DSP_RAM+0x2600,0x40000000); // unsupported master state
        if(kind==8)write_at(g,CTR_DSP_RAM,5); // wrong bank counter
        if(kind==9)CHECK(ctr_memory_change_range(&g->memory,CTR_DSP_RAM+0x20000+0x2000,4096,CTR_MEM_READ,false)==CTR_MEM_OK); // reply bank must be writable
        if(kind==10)write_at(g,CTR_DSP_RAM+0x100+0x34,0x3dffffff); // below 1/8
        if(kind==11)write_at(g,CTR_DSP_RAM+0x100+0x34,0x40800001); // above 4
        if(kind==12)write_at(g,CTR_DSP_RAM+0x100+0x34,0xbf800000); // negative
        if(kind==13)write_at(g,CTR_DSP_RAM+0x100+0x34,0x3f000000); // variable polyphase unsupported
        uint8_t before[CTR_DSP_RAM_BYTES];memcpy(before,g->storage[7],sizeof(before));ctr_dsp_statistics s;
        CHECK(!ctr_dsp_poll(g,5000000) && g->state==CTR_GUEST_FAULTED);
        CHECK(!memcmp(before,g->storage[7],sizeof(before)) && !ctr_dsp_read(g,other,320));
        CHECK(ctr_dsp_stats(g,&s) && s.frames==0 && s.rejected==1 && s.pending);
        CHECK(s.guest_fault==(uint32_t)g->fault && s.guest_fault_detail==g->fault_detail && s.guest_pc==g->cpu.r[15]);closed(g);
    }
}
static void ownership(void) {
    ctr_guest *g=opened();ctr_dsp_statistics s;
    CHECK(!ctr_dsp_stats(g,(ctr_dsp_statistics*)g->storage[2]));
    CHECK(ctr_dsp_poll(g,5000000));CHECK(g->allocations[0].pins==2);CHECK(!ctr_dsp_read(g,(int16_t*)g->storage[2],320));
    CHECK(!ctr_dsp_read(g,other,319));CHECK(ctr_dsp_read(g,other,320));
    // Duplicated service/session and event handles preserve object identity.
    uint32_t service=g->cpu.r[5],sem=g->cpu.r[9];
    g->cpu.r[1]=service;ctr_guest_svc(g,0x27);uint32_t duplicate=g->cpu.r[1];
    CHECK(duplicate && duplicate!=service);CHECK(ctr_guest_handle_close(g,service));
    g->cpu.r[1]=sem;ctr_guest_svc(g,0x27);uint32_t event=g->cpu.r[1];
    CHECK(ctr_guest_handle_close(g,sem));CHECK(ctr_guest_signal(g,event));
    CHECK(ctr_guest_handle_close(g,duplicate));CHECK(g->allocations[0].pins==0);CHECK(ctr_dsp_stats(g,&s) && !s.loaded && !s.pending);
    CHECK(ctr_guest_signal(g,event));closed(g);
    // Queue pressure never overwrites unread output.
    g=opened();for(unsigned i=1;i<=8;++i)CHECK(ctr_guest_run(g,1000000,i*5000000u,false)==CTR_GUEST_WAITING);
    CHECK(ctr_guest_run(g,1000000,45000000,false)==CTR_GUEST_FAULTED);
    CHECK(ctr_dsp_stats(g,&s) && s.frames==8 && s.rejected==1);
    for(unsigned i=0;i<8;++i) { CHECK(ctr_dsp_read(g,other,320)); }
    CHECK(!ctr_dsp_read(g,other,320));closed(g);
}
static void live_resampling(const int16_t *expected,size_t frames) {
    ctr_guest *g=opened();ctr_dsp_stream converter;ctr_dsp_stream_init(&converter);
    static int16_t streamed[192000];size_t written=0;unsigned blocks=0;
    for(unsigned turns=0;turns<400 && g->state!=CTR_GUEST_EXITED;++turns) {
        uint64_t deadline;CHECK(ctr_dsp_next_deadline(g,&deadline) && deadline>=g->now_ns);
        CHECK(ctr_guest_run(g,1000000,deadline,true)!=CTR_GUEST_FAULTED);
        while(ctr_dsp_read(g,other,320)) {
            CHECK(ctr_dsp_stream_push(&converter,other,160));++blocks;
        }
        size_t count;
        do {
            CHECK(ctr_dsp_stream_pull(&converter,streamed+written*2,257,&count));written+=count;
        } while(count);
    }
    CHECK(g->state==CTR_GUEST_EXITED && blocks==300 && ctr_dsp_stream_finish(&converter));
    while(!ctr_dsp_stream_done(&converter)) {
        size_t count;CHECK(ctr_dsp_stream_pull(&converter,streamed+written*2,257,&count) && count);written+=count;
    }
    CHECK(written==frames && !memcmp(streamed,expected,frames*4));closed(g);
}
int main(int argc,char **argv) {
    exact_deadlines_and_ipc();controls_and_queue();variable_rates();guest_adpcm();atomic_failures();ownership();
    ctr_dsp_statistics a,b;uint64_t ai,bi;
    CHECK(ctr_dsp_fixture_render(false,pcm,96000,&a,&ai));
    CHECK(ctr_dsp_fixture_render(true,other,96000,&b,&bi));
    CHECK(!memcmp(pcm,other,sizeof(pcm)) && ai==bi && !memcmp(&a,&b,sizeof(a)));
    CHECK(a.frames==300 && a.samples==48000 && a.output_reads==300 && a.queued==2 && a.loops==4 && !a.rejected && !a.pending && !a.running);
    for(unsigned i=0;i<48000;++i) {
        /* ARM writes pairs of equal samples while its countdown runs 8192..1.
         * Voice 0 switches every 64 samples; voice 1 every 32 samples. */
        unsigned position=i%16384,word=8192-position/2;
        int16_t left=(word&32)?-6000:6000,right=(word&16)?-6000:6000;
        CHECK(pcm[i*2]==(i<16000 || i>=32000?left:0));
        CHECK(pcm[i*2+1]==(i>=16000?right:0));
    }
    static int16_t resampled[192000];size_t written=0;
    CHECK(ctr_dsp_pcm48k(pcm,48000,resampled,96000,&written) && written==70398);
    live_resampling(resampled,written);
    size_t sentinel=7;CHECK(!ctr_dsp_pcm48k(pcm,48000,pcm,96000,&sentinel) && sentinel==7);
    CHECK(!ctr_dsp_pcm48k(pcm,48000,resampled,1,&sentinel) && sentinel==7);
    bool binary=argc==2 && !strcmp(argv[1],"--pcm");
    if(binary) {
#ifdef _WIN32
        CHECK(_setmode(_fileno(stdout),_O_BINARY)!=-1);
#endif
        FILE *f=stdout;
        for(size_t i=0;i<written*2;++i) { uint16_t v=(uint16_t)resampled[i];uint8_t bytes[2]={(uint8_t)v,(uint8_t)(v>>8)};CHECK(fwrite(bytes,1,2,f)==2); }
        CHECK(fflush(f)==0);
    } else CHECK(argc==1);
    fprintf(binary?stderr:stdout,"{\"ok\":true,\"assertions\":%u,\"arm_instructions\":%llu,\"guest_dsp_blocks\":300,\"exact_pcm_frames\":48000,\"console_verified\":false}\n",assertions,(unsigned long long)ai);
}
