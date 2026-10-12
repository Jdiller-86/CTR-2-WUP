/* Original byte-stream harness for cross-architecture/reference validation. */
#include "ctr_texture.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    unsigned format=(unsigned)strtoul(argv[1],NULL,10),width=(unsigned)strtoul(argv[2],NULL,10),height=(unsigned)strtoul(argv[3],NULL,10);
    size_t size;if(ctr_texture_size(format,width,height,&size)!=CTR_TEXTURE_OK)return 3;
    size_t output_size=(size_t)width*height*4;
    uint8_t *input=malloc(size),*output=malloc(output_size);
    if(!input || !output) { free(input);free(output);return 4; }
    int result=0;
    if(fread(input,1,size,stdin)!=size || fgetc(stdin)!=EOF)result=5;
    else if(ctr_texture_decode(input,size,format,width,height,output,output_size)!=CTR_TEXTURE_OK)result=6;
    else if(fwrite(output,1,output_size,stdout)!=output_size)result=7;
    free(input);free(output);return result;
}
