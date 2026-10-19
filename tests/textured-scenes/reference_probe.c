/* Original owned-input probe for the independent rational scene validator. */
#include "ctr_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPACITY (4u*1024u*1024u)
static unsigned char *read_file(const char *name,size_t *size) {
    FILE *file=fopen(name,"rb");if(!file)return NULL;
    unsigned char *data=malloc(CAPACITY+1u);if(!data) { fclose(file);return NULL; }
    *size=fread(data,1,CAPACITY+1u,file);
    int ok=!ferror(file) && *size<=CAPACITY;
    if(fclose(file))ok=0;
    if(!ok) { free(data);return NULL; }
    return data;
}
int main(int argc,char **argv) {
    if(argc!=5 && argc!=6)return 1;
    unsigned side=64;
    if(argc==6) {
        char *end;unsigned long value=strtoul(argv[5],&end,10);
        if(*end || value<8 || value>512 || (value&(value-1u)))return 1;
        side=(unsigned)value;
    }
    size_t pixel_bytes=(size_t)side*side*4u;
    size_t command_size=0,geometry_size=0,texture_size=0;
    unsigned char *commands=read_file(argv[1],&command_size);
    unsigned char *geometry=read_file(argv[2],&geometry_size);
    unsigned char *texture=read_file(argv[3],&texture_size);
    unsigned char *pixels=malloc(pixel_bytes),*before=malloc(pixel_bytes);
    ctr_scene_state *state=malloc(sizeof(*state)),*previous=malloc(sizeof(*previous));
    int exit_code=2;
    if(!commands || !geometry || !texture || !state || !previous || !pixels || !before)goto done;
    for(size_t i=0;i<pixel_bytes;++i) {
        static const unsigned char background[4]={16,32,48,255};
        pixels[i]=background[i&3u];
    }
    memcpy(before,pixels,pixel_bytes);ctr_scene_init(state);memcpy(previous,state,sizeof(*state));
    ctr_scene_resource_span span={UINT32_C(0x1a000000),texture,texture_size};
    ctr_scene_resources resources={UINT32_C(0x18000000),geometry,geometry_size,&span,1u};
    ctr_scene_surface surface={pixels,pixel_bytes,side,side,side*4u};
    ctr_scene_report report={0};
    ctr_scene_result result=ctr_scene_execute(state,commands,command_size,&resources,&surface,&report);
    FILE *output=fopen(argv[4],"wb");if(!output)goto done;
    int saved=fwrite(pixels,1,pixel_bytes,output)==pixel_bytes;
    if(fclose(output))saved=0;
    if(!saved)goto done;
    printf("{\"result\":\"%s\",\"draws\":%u,\"triangles\":%u,\"fragments\":%u,"
           "\"shader_instructions\":%u,\"state_unchanged\":%s,\"pixels_unchanged\":%s,"
           "\"byte_offset\":%zu,\"register\":%u}\n",ctr_scene_result_name(result),
           report.draws,report.triangles,report.fragments,report.shader_instructions,
           memcmp(state,previous,sizeof(*state))?"false":"true",
           memcmp(pixels,before,pixel_bytes)?"false":"true",report.byte_offset,report.reg);
    exit_code=0;
done:
    free(commands);free(geometry);free(texture);free(state);free(previous);free(pixels);free(before);return exit_code;
}
