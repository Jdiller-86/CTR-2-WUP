/* Original batch adapter: exercise public execution, never a private helper. */
#include "ctr_shader.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    unsigned op;uint32_t a,b;
    while(scanf("%u %" SCNx32 " %" SCNx32,&op,&a,&b)==3) {
        ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out,previous;
        if(op!=0u && op!=8u)return 1;
        p.code[0]=(op<<26)|(1u<<7);p.code[1]=UINT32_C(0x88000000);
        p.code_defined[0]=p.code_defined[1]=p.descriptor_defined[0]=1;
        p.descriptors[0]=8;input.enabled=3;
        memcpy(&input.input[0][0],&a,4);memcpy(&input.input[1][0],&b,4);
        memset(&out,0xa5,sizeof(out));previous=out;
        ctr_shader_result status=ctr_shader_execute(&p,&input,&out);
        uint32_t bits=0;memcpy(&bits,&out.values[0][0],4);
        if(status!=CTR_SHADER_OK && memcmp(&out,&previous,sizeof(out)))return 2;
        if(status==CTR_SHADER_OK && (out.written[0]!=1 || out.instructions!=2))return 3;
        printf("%s %08" PRIx32 "\n",ctr_shader_result_name(status),bits);
    }
    return ferror(stdin) || ferror(stdout)?4:0;
}
