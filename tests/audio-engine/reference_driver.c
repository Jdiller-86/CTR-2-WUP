#include "ctr_audio.h"
#include "ctr_audio_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t get32(FILE *f) {
    uint32_t n=0;for(unsigned i=0;i<4;++i) { int c=fgetc(f);if(c==EOF)exit(2);n|=(uint32_t)c<<(i*8); }return n;
}
static void put32(FILE *f,uint32_t n) { for(unsigned i=0;i<4;++i)fputc((int)((n>>(i*8))&255u),f); }
static void put64(FILE *f,uint64_t n) { put32(f,(uint32_t)n);put32(f,(uint32_t)(n>>32)); }
static void put16(FILE *f,int16_t n) { unsigned v=(uint16_t)n;fputc((int)(v&255u),f);fputc((int)(v>>8),f); }
static void snapshot(FILE *f,const CtrAudioEngine *e) {
    put64(f,e->rendered_frames);put64(f,e->clipped_samples);
    for(unsigned i=0;i<2;++i) {
        const CtrAudioVoice *v=&e->voices[i];
        put32(f,(v->bound?1u:0u)|(v->playing?2u:0u));put32(f,v->position);put32(f,v->phase);
        put32(f,v->gain[0]);put32(f,v->gain[1]);put64(f,v->rendered_frames);put64(f,v->clipped_samples);
    }
}
static int fixtures(const char *directory) {
    static CtrAudioFixture fixture,before;
    memset(&fixture,0x5a,sizeof(fixture));before=fixture;
    if(ctr_audio_fixture_build(NULL,1) || ctr_audio_fixture_build(&fixture,0) ||
       ctr_audio_fixture_build(&fixture,3) || ctr_audio_fixture_build_rate(&fixture,1,7999) ||
       ctr_audio_fixture_build_rate(&fixture,1,96001) ||
       ctr_audio_fixture_build_mono(&fixture,32000,2) || memcmp(&fixture,&before,sizeof(fixture)))return 1;
    static const char *names[5]={"mono32k.cstm","stereo32k.cstm","stereo48k.cstm","mono32k-falling.cstm","mono48k.cstm"};
    for(unsigned i=0;i<5;++i) {
        bool ok=i==3?ctr_audio_fixture_build_mono(&fixture,32000,1):
            ctr_audio_fixture_build_rate(&fixture,i==1 || i==2?2:1,i==2 || i==4?48000:32000);
        if(!ok)return 1;
        if(!i) {
            before=fixture;
            if(!ctr_audio_fixture_build(&fixture,1) || memcmp(&fixture,&before,sizeof(fixture)))return 1;
        }
        char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",directory,names[i]);
        if(n<0 || (size_t)n>=sizeof(path))return 2;
        FILE *f=fopen(path,"wb");if(!f)return 2;
        if(fwrite(fixture.data,1,fixture.bytes,f)!=fixture.bytes || fclose(f))return 2;
    }
    return 0;
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    if(!strcmp(argv[1],"--fixtures"))return fixtures(argv[2]);
    FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");if(!in || !out)return 2;
    char magic[4];if(fread(magic,1,4,in)!=4 || memcmp(magic,"AEM1",4))return 2;
    uint32_t cases=get32(in);if(cases>2048)return 2;put32(out,cases);
    for(uint32_t row=0;row<cases;++row) {
        CtrAudioEngine engine;if(ctr_audio_init(&engine)!=CTR_AUDIO_OK)return 1;
        uint8_t *sources[2]={NULL,NULL};
        for(unsigned v=0;v<2;++v) {
            uint32_t n=get32(in);if(!n || n>65536)return 2;
            sources[v]=malloc(n);if(!sources[v] || fread(sources[v],1,n,in)!=n)return 2;
            CtrAudioResult r=ctr_audio_bind(&engine,v,sources[v],n);
            if(r!=CTR_AUDIO_OK) { fprintf(stderr,"case %u bind %u: %s\n",row,v,ctr_audio_result_name(r));return 1; }
        }
        uint32_t commands=get32(in);if(commands>2048)return 2;put32(out,commands);
        for(uint32_t step=0;step<commands;++step) {
            uint32_t kind=get32(in),voice=get32(in),a=get32(in),b=get32(in);
            CtrAudioResult r;int16_t pcm[CTR_AUDIO_MAX_FRAMES*2];
            switch(kind) {
            case 1:r=ctr_audio_gain(&engine,voice,a,b);break;
            case 2:r=ctr_audio_play(&engine,voice,a!=0);break;
            case 3:r=ctr_audio_seek(&engine,voice,a);break;
            case 4:
                if(a>CTR_AUDIO_MAX_FRAMES)return 2;
                r=ctr_audio_render(&engine,pcm,a,a);break;
            default:return 2;
            }
            if(r!=CTR_AUDIO_OK) { fprintf(stderr,"case %u command %u kind %u: %s\n",row,step,kind,ctr_audio_result_name(r));return 1; }
            put32(out,(uint32_t)r);
            if(kind==4)for(uint32_t n=0;n<a*2;++n)put16(out,pcm[n]);
            snapshot(out,&engine);
        }
        free(sources[0]);free(sources[1]);
    }
    if(fgetc(in)!=EOF || ferror(in) || fclose(in) || ferror(out) || fclose(out))return 2;
    return 0;
}
