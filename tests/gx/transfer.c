/* Original byte-stream harness for cross-architecture image comparisons. */
#include "ctr_transfer.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    uint32_t dim=(uint32_t)strtoul(argv[1],NULL,0),flags=(uint32_t)strtoul(argv[2],NULL,0);
    ctr_transfer t;if(!ctr_transfer_init(&t,dim,dim,flags))return 3;
    uint8_t *in=malloc(t.input_bytes),*out=malloc(t.output_bytes);
    if(!in || !out) { free(in);free(out);return 4; }
    int result=0;
    if(fread(in,1,t.input_bytes,stdin)!=t.input_bytes || fgetc(stdin)!=EOF)result=5;
    else if(!ctr_transfer_convert(dim,dim,flags,in,t.input_bytes,out,t.output_bytes))result=6;
    else if(fwrite(out,1,t.output_bytes,stdout)!=t.output_bytes)result=7;
    free(in);free(out);return result;
}
