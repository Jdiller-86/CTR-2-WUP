/* Original test-only adapter; links unmodified, audited external libctru gpu.c.
 * It tests command construction only. No 3DS or Wii U GPU execution occurs. */
#include <3ds/types.h>
#include <3ds/svc.h>
#include <3ds/gpu/gpu.h>
#include <stdio.h>
#include <stdlib.h>

void svcBreak(UserBreakType reason) { (void)reason; abort(); }

static int read_word(u32 *value) {
    unsigned char bytes[4];
    if (fread(bytes,1,sizeof(bytes),stdin)!=sizeof(bytes)) return 0;
    *value=(u32)bytes[0]|((u32)bytes[1]<<8)|((u32)bytes[2]<<16)|((u32)bytes[3]<<24);
    return 1;
}
static int write_word(u32 value) {
    unsigned char bytes[4]={(unsigned char)value,(unsigned char)(value>>8),
                            (unsigned char)(value>>16),(unsigned char)(value>>24)};
    return fwrite(bytes,1,sizeof(bytes),stdout)==sizeof(bytes);
}
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    char *end;
    unsigned long header=strtoul(argv[1],&end,0);
    if(*end || header>UINT32_MAX) return 2;
    unsigned long count=strtoul(argv[2],&end,0);
    if(*end || count>8192) return 2;
    unsigned long finalize=strtoul(argv[3],&end,0);
    if(*end || finalize>1) return 2;
    /* Leave room for libctru's 256-parameter chunk headers/padding and tails. */
    u32 parameters[8192]={0},buffer[16384]={0};
    unsigned long supplied=count ? count : 1;
    for(unsigned long i=0;i<supplied;++i) if(!read_word(&parameters[i])) return 3;
    if(fgetc(stdin)!=EOF) return 3;
    GPUCMD_SetBuffer(buffer,sizeof(buffer)/sizeof(buffer[0]),0);
    GPUCMD_Add((u32)header,parameters,(u32)count);
    u32 *start=buffer,size=gpuCmdBufOffset;
    if(finalize) GPUCMD_Split(&start,&size);
    if(start!=buffer || size>sizeof(buffer)/sizeof(buffer[0])) return 4;
    for(u32 i=0;i<size;++i) if(!write_word(start[i])) return 5;
    return 0;
}
