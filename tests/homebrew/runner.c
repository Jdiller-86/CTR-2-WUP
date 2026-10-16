/* Checked local runner for original compiler-built applications. */
#include "boot-image.h"
#include "ctr_recomp.h"
#include "ctr_gsp.h"
#include "ctr_hid.h"
#include "ctr_fs_snapshot.h"
#include "ctr_romfs_file.h"
#include "ctr_sha256.h"
#include <string.h>
#ifdef CTR_ROMFS_HOMEBREW
#include "romfs-image.h"
static bool romfs_read(void *context,uint64_t offset,void *output,uint32_t size) {
    (void)context;
    if(offset>sizeof(romfs_bytes) || size>sizeof(romfs_bytes)-offset)return false;
    memcpy(output,romfs_bytes+(size_t)offset,size);return true;
}
#endif
#include "ctr_apt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char messages[1024];static unsigned used;
static int pending_input=-1;static unsigned input_samples;
static bool interactive;static unsigned input_stage;
#ifdef CTR_DRAW_HOMEBREW
static bool draw_ready;
static uint32_t draw_pixel(unsigned x,unsigned y,unsigned screen) {
    unsigned height=screen?320u:400u,px=239u-y,py=x;
    if(px<30u || px>=210u || py<height/8u || py>=height*7u/8u)return UINT32_C(0x102031ff);
#ifdef CTR_TEXTURED_HOMEBREW
    unsigned tx=((2u*px+1u-60u)*16u)/360u;
    unsigned ty=7u-((2u*py+1u-height/4u)*8u)/(height*3u/2u);
    uint32_t result=((tx&1u)?UINT32_C(0xff000000):0u)|
        ((((tx>>2)^(ty>>1))&1u)?UINT32_C(0x00ff0000):0u)|
        ((((tx>>3)^ty)&1u)?UINT32_C(0x0000ff00):0u)|255u;
    if((2u*px+1u)*height+(2u*py+1u)*240u>=480u*height)result&=UINT32_C(0x00ff00ff);
    return result;
#else
    return (2u*px+1u)*height+(2u*py+1u)*240u<480u*height?UINT32_C(0xff0000ff):UINT32_C(0x00ff00ff);
#endif
}
#endif
static void message(void *context,const char *text) {
    (void)context;size_t size=strlen(text);if(size+used+2<sizeof(messages)) { memcpy(messages+used,text,size);used+=(unsigned)size;messages[used++]=';'; }
    if(size==21 && !strncmp(text,"CTR2WUP INPUT READY ",20) && text[20]>='0' && text[20]<='2')pending_input=text[20]-'0';
    if(!strcmp(text,"CTR2WUP INTERACTIVE READY")) { interactive=true;pending_input=0; }
#ifdef CTR_DRAW_HOMEBREW
    if(!strcmp(text,"CTR2WUP DRAW READY"))draw_ready=true;
#endif
}
static void trace(void *context,const char *event,const char *hash,uint32_t pc,bool thumb,uint32_t detail) {
    (void)context;(void)hash;(void)thumb;
    if(strcmp(event,"svc"))fprintf(stderr,"%s pc=%08x detail=%08x\n",event,pc,detail);
}
static unsigned captures;static uint32_t colors[16];
static bool capture(ctr_guest *g,const char *folder) {
    static uint64_t swaps[2];
    if(!folder || g->gsp.black || !g->gsp.active[0] || !g->gsp.active[1] ||
       (swaps[0]==g->gsp.swaps[0] && swaps[1]==g->gsp.swaps[1]))return true;
#ifdef CTR_DRAW_HOMEBREW
    if(!draw_ready)return true;
#endif
    uint32_t *pixels=malloc(400*240*sizeof(*pixels));if(!pixels)return false;
    bool match=ctr_gsp_pixels(g,0,pixels,400*240)==CTR_MEM_OK;
    uint32_t color=match?pixels[0]:0;
#ifdef CTR_DRAW_HOMEBREW
    for(unsigned i=0;match && i<400*240;++i)match=pixels[i]==draw_pixel(i%400u,i/400u,0);
    match=match && ctr_gsp_pixels(g,1,pixels,320*240)==CTR_MEM_OK;
    for(unsigned i=0;match && i<320*240;++i)match=pixels[i]==draw_pixel(i%320u,i/320u,1);
    if(!match) { free(pixels);return false; }
#else
    match=match && (color==0x00ff00ff || color==0xff0000ff);
    for(unsigned i=0;match && i<400*240;++i)match=pixels[i]==color;
    match=match && ctr_gsp_pixels(g,1,pixels,320*240)==CTR_MEM_OK;
    for(unsigned i=0;match && i<320*240;++i) {
        unsigned touch=input_stage==0?23*320+17:239*320+319;
        uint32_t expected=interactive && input_stage<2 && i==touch?0xffffffff:0x0000ffff;
        match=pixels[i]==expected;
    }
#endif
    if(match) {
        if(captures==16) { free(pixels);return false; }
        colors[captures]=color;
        for(unsigned screen=0;screen<2;++screen) {
            unsigned width=screen?320:400;char path[4096];
            int size=snprintf(path,sizeof(path),"%s/frame-%u-%s.ppm",folder,captures,screen?"bottom":"top");
            if(size<0 || (size_t)size>=sizeof(path) || ctr_gsp_pixels(g,screen,pixels,width*240)!=CTR_MEM_OK) { free(pixels);return false; }
            FILE *file=fopen(path,"wb");if(!file) { free(pixels);return false; }
            bool ok=fprintf(file,"P6\n%u 240\n255\n",width)>0;
            for(unsigned i=0;ok && i<width*240;++i) {
                uint8_t rgb[3]={(uint8_t)(pixels[i]>>24),(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8)};
                ok=fwrite(rgb,1,3,file)==3;
            }
            if(fclose(file))ok=false;
            if(!ok) { free(pixels);return false; }
        }
        ++captures;swaps[0]=g->gsp.swaps[0];swaps[1]=g->gsp.swaps[1];
        if(interactive && captures<=3)pending_input=(int)captures;
    }
    free(pixels);return true;
}
int main(int argc,char **argv) {
    bool ipc_trace=argc>1 && !strcmp(argv[1],"--trace-ipc");uint64_t requests=0;
    bool filesystem=argc>2 && !strcmp(argv[1],"--filesystem");
    ctr_guest g;if(!ctr_guest_open(&g,&boot_image,NULL,0))return 1;
    if(!ctr_apt_configure(&g)) { ctr_guest_close(&g);return 7; }
    ctr_fs_volume sd;
    if(!ctr_fs_volume_init(&sd,9,false) || !ctr_fs_mount(&g,&sd)) { ctr_guest_close(&g);return 8; }
    ctr_fs_volume save;
#ifdef CTR_ROMFS_HOMEBREW
    ctr_fs_romfs assets={0};ctr_romfs_file asset_file={0};
    bool external_assets=argc==3 && !strcmp(argv[1],"--romfs-file");
    ctr_sha256 hash;uint8_t digest[32];ctr_sha256_init(&hash);
    bool asset_ok=external_assets?ctr_romfs_file_open(&asset_file,argv[2],romfs_sha256):
        ctr_sha256_update(&hash,romfs_bytes,sizeof(romfs_bytes)) && ctr_sha256_final(&hash,digest) &&
        !memcmp(digest,romfs_sha256,32) && ctr_fs_romfs_init(&assets,sizeof(romfs_bytes),romfs_read,NULL);
    if(!asset_ok || !ctr_fs_mount_romfs(&g,external_assets?&asset_file.view:&assets)) {
        ctr_guest_close(&g);ctr_fs_volume_close(&sd);ctr_romfs_file_close(&asset_file);return 9;
    }
#endif
    if(filesystem) {
        ctr_fs_volume_init(&save,4,true);
        if(ctr_fs_snapshot_load(&save,argv[2])==CTR_FS_SNAPSHOT_ERROR || !ctr_fs_mount(&g,&save)) { ctr_guest_close(&g);ctr_fs_volume_close(&save);return 5; }
        save.persist=ctr_fs_snapshot_save;save.persist_context=argv[2];
    }
    g.cpu.log=trace;g.debug=message;if(!ctr_recomp_attach(&g,false)) { ctr_guest_close(&g);return 2; }
    for(unsigned n=0;n<(ipc_trace?2000000u:10000u) && g.state!=CTR_GUEST_FAULTED && g.state!=CTR_GUEST_EXITED;++n) {
        uint64_t now=(uint64_t)n*(ipc_trace?1000u:1000000u);
        ctr_guest_run(&g,ipc_trace?1:10000,now,true);
        if(ipc_trace && g.ipc_requests!=requests) {
            fprintf(stderr,"ipc request=%llu service=%s target=%s header=%08x result=%08x pc=%08x\n",(unsigned long long)g.ipc_requests,g.ipc_service[0]?g.ipc_service:"-",g.ipc_target[0]?g.ipc_target:"-",g.ipc_header,g.ipc_result,g.cpu.r[15]);
            requests=g.ipc_requests;
        }
        if(!capture(&g,!ipc_trace && !filesystem && argc>1 && strcmp(argv[1],"--romfs-file")?argv[1]:NULL)) { ctr_guest_close(&g);return 3; }
        if(pending_input>=0) {
            static const int cx[]={-30,100,0,0},cy[]={40,-100,0,0};
            static const uint16_t x[]={17,319,0,0},y[]={23,239,0,0};
            unsigned stage=(unsigned)pending_input;
            uint32_t buttons=interactive?(stage==1?0:stage==3?2:1):(stage==0?1:stage==1?2:0);
            if(!ctr_hid_update(&g,buttons,cx[stage],cy[stage],stage<2,x[stage],y[stage],now+1)) { ctr_guest_close(&g);return 4; }
            ++input_samples;input_stage=stage;pending_input=-1;
        }
    }
    printf("{\"state\":\"%s\",\"fault\":\"%s\",\"pc\":%u,\"instructions\":%llu,\"svcs\":%llu,\"switches\":%llu,\"fallback\":%llu,\"ipc_failures\":%llu,\"messages\":\"%s\",\"frames\":[",ctr_guest_state_name(g.state),ctr_cpu_result_name(g.fault),g.cpu.r[15],(unsigned long long)g.cpu.instructions,(unsigned long long)g.svc_count,(unsigned long long)g.context_switches,(unsigned long long)g.cpu.fallback_instructions,(unsigned long long)g.ipc_failures,messages);
    for(unsigned i=0;i<captures;++i)printf("%s%u",i?",":"",colors[i]);
    unsigned depth=0;uint64_t unwinds=0;ctr_recomp_depth_stats(&g,&depth,&unwinds);
    printf("],\"input_samples\":%u,\"save_commits\":%llu,\"aot_max_depth\":%u,\"aot_unwinds\":%llu,\"gx\":{\"commands\":%llu,\"dma\":%llu,\"fills\":%llu,\"transfers\":%llu,\"copies\":%llu,\"cache\":%llu,\"pica\":%llu},\"pica_state\":{\"lists\":%llu,\"writes\":%llu,\"finalizers\":%llu,\"viewport_width\":%u}",input_samples,(unsigned long long)(filesystem?save.commits:0),depth,(unsigned long long)unwinds,(unsigned long long)g.gsp.gx_commands,(unsigned long long)g.gsp.gx_dma,(unsigned long long)g.gsp.gx_fills,(unsigned long long)g.gsp.gx_transfers,(unsigned long long)g.gsp.gx_copies,(unsigned long long)g.gsp.gx_cache,(unsigned long long)g.gsp.gx_pica,(unsigned long long)g.gsp.pica.committed_lists,(unsigned long long)g.gsp.pica.register_writes,(unsigned long long)g.gsp.pica.finalizations,g.gsp.pica.registers[0x41]);
    printf(",\"pica_render\":{\"draws\":%llu,\"triangles\":%llu,\"fragments\":%llu,\"shader_instructions\":%llu}}\n",
        (unsigned long long)g.gsp.pica_draws,(unsigned long long)g.gsp.pica_triangles,
        (unsigned long long)g.gsp.pica_fragments,(unsigned long long)g.gsp.pica_shader_instructions);
    ctr_guest_close(&g);if(!ctr_fs_volume_close(&sd) || (filesystem && !ctr_fs_volume_close(&save)))return 6;
#ifdef CTR_ROMFS_HOMEBREW
    if(external_assets?!ctr_romfs_file_close(&asset_file):!ctr_fs_romfs_close(&assets))return 10;
#endif
    return 0;
}
