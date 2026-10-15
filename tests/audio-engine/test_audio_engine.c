#include "ctr_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void w16(uint8_t *d,size_t at,int v) { d[at]=(uint8_t)v;d[at+1]=(uint8_t)((unsigned)v>>8); }
static void w32(uint8_t *d,size_t at,uint32_t v) { w16(d,at,(int)v);w16(d,at+2,(int)(v>>16)); }
static void ref(uint8_t *d,size_t at,int type,uint32_t offset) { w16(d,at,type);w32(d,at+4,offset); }
/* Original two-channel 14+3 sample ramp with padded final channel payloads. */
static void fixture(uint8_t d[512],uint32_t rate,int history) {
    memset(d,0,512);memcpy(d,"CSTM",4);w16(d,4,0xfeff);w16(d,6,64);w32(d,8,0x02000000);w32(d,12,512);w16(d,16,3);
    ref(d,20,0x4000,64);w32(d,28,352);ref(d,32,0x4001,416);w32(d,40,32);ref(d,44,0x4002,448);w32(d,52,64);
    memcpy(d+64,"INFO",4);w32(d,68,352);ref(d,72,0x4100,56);ref(d,80,0,UINT32_MAX);ref(d,88,0x0101,120);
    size_t s=128;d[s]=2;d[s+2]=2;w32(d,s+4,rate);w32(d,s+12,17);w32(d,s+16,2);
    w32(d,s+20,8);w32(d,s+24,14);w32(d,s+28,3);w32(d,s+32,3);w32(d,s+36,4);w32(d,s+40,4);w32(d,s+44,14);ref(d,s+48,0x1f00,24);
    w32(d,192,2);ref(d,196,0x4102,32);ref(d,204,0x4102,40);ref(d,224,0x0300,32);ref(d,232,0x0300,80);
    w16(d,256,2048);w16(d,312,2048);w16(d,290,history);w16(d,292,history-1);w16(d,346,-history);w16(d,348,-history+1);
    memcpy(d+416,"SEEK",4);w32(d,420,32);w16(d,424,history);w16(d,426,history-1);w16(d,428,-history);w16(d,430,-history+1);
    w16(d,432,history+14);w16(d,434,history+13);w16(d,436,-history-14);w16(d,438,-history-13);
    memcpy(d+448,"DATA",4);w32(d,452,64);memset(d+481,0x11,7);memset(d+489,0xff,7);
    d[497]=0x11;d[498]=0x1a;d[499]=0x80;d[501]=0xff;d[502]=0xfa;d[503]=0x80;
}
static void start(CtrAudioEngine *e,uint8_t *d,unsigned voices) {
    CHECK(ctr_audio_init(e)==CTR_AUDIO_OK);
    for(unsigned i=0;i<voices;++i) { CHECK(ctr_audio_bind(e,i,d,512)==CTR_AUDIO_OK);CHECK(ctr_audio_play(e,i,true)==CTR_AUDIO_OK); }
}
static void silence_and_unbind(void) {
    CtrAudioEngine e;int16_t output[40];memset(output,0x55,sizeof(output));CHECK(ctr_audio_init(&e)==CTR_AUDIO_OK);
    CHECK(ctr_audio_render(&e,output,20,20)==CTR_AUDIO_OK);for(unsigned i=0;i<40;++i)CHECK(output[i]==0);
    CHECK(e.rendered_frames==20);CHECK(ctr_audio_render(&e,NULL,0,0)==CTR_AUDIO_OK);
    CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_STATE);
    uint8_t d[512];fixture(d,48000,10);CHECK(ctr_audio_bind(&e,0,d,512)==CTR_AUDIO_OK);
    CHECK(!e.voices[0].playing && e.voices[0].gain[0]==32768);
    CHECK(ctr_audio_render(&e,output,20,3)==CTR_AUDIO_OK);CHECK(e.voices[0].position==0);
    CHECK(ctr_audio_bind(&e,0,NULL,0)==CTR_AUDIO_OK);CHECK(!e.voices[0].bound && !e.voices[0].data);
}
static void continuous_and_seek(void) {
    uint8_t d[512];fixture(d,48000,10);
    /* Deliberately different later SEEK metadata: continuous state must win. */
    w16(d,432,1000);w16(d,434,999);w16(d,436,-1000);w16(d,438,-999);
    CtrAudioEngine e;start(&e,d,1);int16_t output[40];
    CHECK(ctr_audio_render(&e,output,20,20)==CTR_AUDIO_OK);
    for(unsigned i=0;i<20;++i) { CHECK(output[2*i]==(i<17?(int)(11+i):0));CHECK(output[2*i+1]==(i<17?-(int)(11+i):0)); }
    CHECK(e.voices[0].position==17 && !e.voices[0].playing && e.voices[0].decoded_frames==17);
    CHECK(e.voices[0].rendered_frames==17 && e.rendered_frames==20);
    CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_OK && !e.voices[0].playing);
    CHECK(ctr_audio_seek(&e,0,14)==CTR_AUDIO_OK && !e.voices[0].playing);
    CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_OK);
    CHECK(ctr_audio_render(&e,output,20,4)==CTR_AUDIO_OK);
    for(unsigned i=0;i<3;++i) { CHECK(output[2*i]==1001+(int)i);CHECK(output[2*i+1]==-1001-(int)i); }
    CHECK(output[6]==0 && output[7]==0);
    CHECK(ctr_audio_seek(&e,0,16)==CTR_AUDIO_OK);CHECK(e.voices[0].decoded_frames==22);
    CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_OK);CHECK(ctr_audio_render(&e,output,20,1)==CTR_AUDIO_OK);
    CHECK(output[0]==1003 && output[1]==-1003);
    CHECK(ctr_audio_seek(&e,0,17)==CTR_AUDIO_OK && !e.voices[0].playing);
}
static int round_fraction(int numerator,int denominator) {
    int n=numerator+denominator/2;return n>=0?n/denominator:-((-n+denominator-1)/denominator);
}
static void rates_and_chunks(void) {
    const uint32_t rates[]={8000,16000,22050,32000,44100,48000,64000,96000};
    for(unsigned rate=0;rate<sizeof(rates)/sizeof(*rates);++rate) {
        uint8_t d[512];fixture(d,rates[rate],10);CtrAudioEngine full,chunked;start(&full,d,1);start(&chunked,d,1);
        int16_t a[240],b[240];CHECK(ctr_audio_render(&full,a,120,120)==CTR_AUDIO_OK);
        unsigned offset=0;
        while(offset<120) { unsigned n=(offset%7)+1;if(n>120-offset)n=120-offset;CHECK(ctr_audio_render(&chunked,b+offset*2,n,n)==CTR_AUDIO_OK);offset+=n; }
        CHECK(!memcmp(a,b,sizeof(a)));CHECK(!memcmp(&full,&chunked,sizeof(full)));
        for(unsigned i=0;i<120;++i) {
            uint32_t pos=(uint32_t)((uint64_t)i*rates[rate]/48000);
            uint32_t phase=(uint32_t)((uint64_t)i*rates[rate]%48000);
            int expected=0;
            if(pos<17)expected=round_fraction((int)(11+pos)*48000+(pos<16?(int)phase:0),48000);
            CHECK(a[2*i]==expected);
            /* Negative ties round toward +infinity independently. */
            if(pos<17)expected=round_fraction(-(int)(11+pos)*48000-(pos<16?(int)phase:0),48000);
            CHECK(a[2*i+1]==expected);
        }
        CHECK(full.voices[0].rendered_frames==((uint64_t)17*48000+rates[rate]-1)/rates[rate]);
    }
}
static void mono_and_maximum(void) {
    uint8_t d[512];fixture(d,48000,10);
    d[130]=1;w32(d,192,1);w16(d,428,24);w16(d,430,23);
    d[488]=0;d[489]=0x11;d[490]=0x1a;d[491]=0x80;
    CtrAudioEngine e;start(&e,d,1);int16_t output[CTR_AUDIO_MAX_FRAMES*2];
    CHECK(ctr_audio_render(&e,output,CTR_AUDIO_MAX_FRAMES,CTR_AUDIO_MAX_FRAMES)==CTR_AUDIO_OK);
    for(unsigned i=0;i<CTR_AUDIO_MAX_FRAMES;++i) {
        CHECK(output[2*i]==(i<17?(int)(11+i):0));CHECK(output[2*i]==output[2*i+1]);
    }
    CHECK(e.rendered_frames==CTR_AUDIO_MAX_FRAMES && e.voices[0].rendered_frames==17);
    CHECK(ctr_audio_seek(&e,0,13)==CTR_AUDIO_OK);CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_OK);
    CHECK(ctr_audio_render(&e,output,10,5)==CTR_AUDIO_OK);
    for(unsigned i=0;i<4;++i)CHECK(output[2*i]==24+(int)i && output[2*i+1]==24+(int)i);
    CHECK(output[8]==0 && output[9]==0);
}
static void gain_mix_pause(void) {
    uint8_t d[512];fixture(d,48000,10);CtrAudioEngine e;start(&e,d,1);int16_t output[40];
    CHECK(ctr_audio_gain(&e,0,16384,16384)==CTR_AUDIO_OK);CHECK(ctr_audio_render(&e,output,20,1)==CTR_AUDIO_OK);
    CHECK(output[0]==6 && output[1]==-5);
    CHECK(ctr_audio_play(&e,0,false)==CTR_AUDIO_OK);CtrAudioVoice before=e.voices[0];
    CHECK(ctr_audio_render(&e,output,20,5)==CTR_AUDIO_OK);CHECK(!memcmp(&before,&e.voices[0],sizeof(before)));
    for(unsigned i=0;i<10;++i)CHECK(output[i]==0);
    CHECK(ctr_audio_play(&e,0,true)==CTR_AUDIO_OK);CHECK(ctr_audio_gain(&e,0,0,32768)==CTR_AUDIO_OK);
    CHECK(ctr_audio_render(&e,output,20,1)==CTR_AUDIO_OK && output[0]==0 && output[1]==-12);
    fixture(d,48000,20000);start(&e,d,2);CHECK(ctr_audio_render(&e,output,20,20)==CTR_AUDIO_OK);
    for(unsigned i=0;i<17;++i)CHECK(output[2*i]==32767 && output[2*i+1]==-32768);
    CHECK(e.clipped_samples==34 && e.voices[0].clipped_samples==34 && e.voices[1].clipped_samples==34);
    CHECK(e.voices[0].rendered_frames==17 && e.voices[1].rendered_frames==17);
    fixture(d,48000,10);start(&e,d,1);e.rendered_frames=UINT64_MAX-1;e.voices[0].rendered_frames=UINT64_MAX-1;
    e.voices[0].decoded_frames=UINT64_MAX-1;CHECK(ctr_audio_render(&e,output,20,5)==CTR_AUDIO_OK);
    CHECK(e.rendered_frames==UINT64_MAX && e.voices[0].rendered_frames==UINT64_MAX && e.voices[0].decoded_frames==UINT64_MAX);
}
static void atomic_faults(void) {
    uint8_t d[512];fixture(d,48000,10);CtrAudioEngine e;start(&e,d,1);int16_t output[40],prior_output[40];
    memset(output,0x53,sizeof(output));memcpy(prior_output,output,sizeof(output));
    d[500]=0x80;CtrAudioEngine prior=e;
    CHECK(ctr_audio_render(&e,output,20,20)==CTR_AUDIO_PREDICTOR);CHECK(!memcmp(&e,&prior,sizeof(e)));CHECK(!memcmp(output,prior_output,sizeof(output)));
    CHECK(ctr_audio_render(&e,output,20,10)==CTR_AUDIO_OK);prior=e;memset(output,0x53,sizeof(output));
    CHECK(ctr_audio_render(&e,output,20,10)==CTR_AUDIO_PREDICTOR);CHECK(!memcmp(&e,&prior,sizeof(e)));CHECK(!memcmp(output,prior_output,sizeof(output)));
    CHECK(ctr_audio_seek(&e,0,16)==CTR_AUDIO_PREDICTOR);CHECK(!memcmp(&e,&prior,sizeof(e)));
    d[500]=0;d[0]='X';CHECK(ctr_audio_render(&e,output,20,1)==CTR_AUDIO_FORMAT);CHECK(!memcmp(&e,&prior,sizeof(e)));
    d[0]='C';w32(d,132,32000);CHECK(ctr_audio_render(&e,output,20,1)==CTR_AUDIO_STATE);CHECK(!memcmp(&e,&prior,sizeof(e)));w32(d,132,48000);
    CHECK(ctr_audio_gain(&e,0,32769,0)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_seek(&e,0,18)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,output,0,1)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,output,4097,4097)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,NULL,1,1)==CTR_AUDIO_ARGUMENT);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,(int16_t *)d,1,1)==CTR_AUDIO_OVERLAP);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,(int16_t *)&e,1,1)==CTR_AUDIO_OVERLAP);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_bind(&e,0,(uint8_t *)&e,sizeof(e))==CTR_AUDIO_OVERLAP);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_render(&e,(int16_t *)(UINTPTR_MAX-1),1,1)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_bind(&e,0,(uint8_t *)(UINTPTR_MAX-1),512)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_bind(&e,0,NULL,512)==CTR_AUDIO_ARGUMENT);CHECK(!memcmp(&e,&prior,sizeof(e)));
    CHECK(ctr_audio_bind(&e,2,d,512)==CTR_AUDIO_BOUNDS);CHECK(!memcmp(&e,&prior,sizeof(e)));
    fixture(d,7999,10);CHECK(ctr_audio_bind(&e,0,d,512)==CTR_AUDIO_STATE); /* existing binding changed metadata */
    CHECK(ctr_audio_init(&e)==CTR_AUDIO_OK);CHECK(ctr_audio_bind(&e,0,d,512)==CTR_AUDIO_UNSUPPORTED);
    fixture(d,96001,10);CHECK(ctr_audio_bind(&e,0,d,512)==CTR_AUDIO_UNSUPPORTED);
    CHECK(ctr_audio_init(NULL)==CTR_AUDIO_ARGUMENT);CHECK(ctr_audio_init((CtrAudioEngine *)(UINTPTR_MAX-1))==CTR_AUDIO_BOUNDS);
    CHECK(ctr_audio_render(NULL,output,1,1)==CTR_AUDIO_ARGUMENT);
    memset(&e,0,sizeof(e));CHECK(ctr_audio_render(&e,output,1,1)==CTR_AUDIO_STATE);
    for(int i=0;i<=CTR_AUDIO_STATE;++i)CHECK(strcmp(ctr_audio_result_name((CtrAudioResult)i),"invalid"));
    CHECK(!strcmp(ctr_audio_result_name((CtrAudioResult)99),"invalid"));
}
int main(void) {
    silence_and_unbind();continuous_and_seek();rates_and_chunks();mono_and_maximum();gain_mix_pause();atomic_faults();
    printf("Audio engine: %u checks passed; engine storage %zu bytes\n",checks,sizeof(CtrAudioEngine));return 0;
}
