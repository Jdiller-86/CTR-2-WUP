/* Original public-API adapter. All shader inputs arrive as bounded text words. */
#include "ctr_gx2_shader.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static ctr_shader_program program;
static ctr_gx2_shader compiled, previous;
static ctr_vertex input;
static ctr_shader_output output, old_output;

static int word(uint32_t *value) { return scanf("%" SCNx32,value)==1; }
int main(void) {
    unsigned entry,code_count,descriptor_count,uniform_count,mask,count,enabled;
    while(scanf("%u %u %u %u %u %u %u",&entry,&code_count,&descriptor_count,
                &uniform_count,&mask,&count,&enabled)==7) {
        if(code_count>512 || descriptor_count>128 || uniform_count>96 || count>8 || mask>65535 || enabled>65535)return 1;
        memset(&program,0,sizeof(program));memset(&input,0,sizeof(input));
        program.entry=entry;input.enabled=(uint16_t)enabled;
        uint32_t maps[8];for(unsigned i=0;i<8;++i)if(!word(&maps[i]))return 2;
        for(unsigned r=0;r<16;++r)for(unsigned c=0;c<4;++c) {
            uint32_t value;if(!word(&value))return 2;memcpy(&input.input[r][c],&value,4);
        }
        for(unsigned i=0;i<code_count;++i) {
            unsigned at;uint32_t value;
            if(scanf("%u",&at)!=1 || at>=512 || !word(&value))return 2;
            program.code[at]=value;program.code_defined[at]=1;
        }
        for(unsigned i=0;i<descriptor_count;++i) {
            unsigned at;uint32_t value;
            if(scanf("%u",&at)!=1 || at>=128 || !word(&value))return 2;
            program.descriptors[at]=value;program.descriptor_defined[at]=1;
        }
        for(unsigned i=0;i<uniform_count;++i) {
            unsigned at,defined;
            if(scanf("%u %u",&at,&defined)!=2 || at>=96 || defined>15)return 2;
            program.uniform_defined[at]=(uint8_t)defined;
            for(unsigned c=0;c<4;++c)if(!word(&program.uniforms[at][c]))return 2;
        }
        memset(&compiled,0xa5,sizeof(compiled));previous=compiled;
        memset(&output,0xa5,sizeof(output));old_output=output;
        ctr_gx2_shader_result result=ctr_gx2_shader_compile(&program,(uint16_t)mask,maps,count,&compiled);
        ctr_shader_result cpu=ctr_shader_execute(&program,&input,&output);
        if(result!=CTR_GX2_SHADER_OK && memcmp(&compiled,&previous,sizeof(compiled)))return 3;
        if(cpu!=CTR_SHADER_OK && memcmp(&output,&old_output,sizeof(output)))return 4;
        printf("{\"compile\":\"%s\",\"cpu\":\"%s\",\"rollback\":true",ctr_gx2_shader_result_name(result),ctr_shader_result_name(cpu));
        if(result==CTR_GX2_SHADER_OK) {
            if(compiled.size>sizeof(compiled.program) || compiled.size%8)return 5;
            printf(",\"gprs\":%" PRIu32 ",\"input_mask\":%u,\"pica_instructions\":%" PRIu32 ",\"alu_instructions\":%" PRIu32 ",\"bytes\":\"",
                   compiled.gprs,compiled.input_mask,compiled.pica_instructions,compiled.alu_instructions);
            for(unsigned i=0;i<compiled.size;++i)printf("%02x",compiled.program[i]);
            printf("\",\"uniform_used\":[");
            for(unsigned i=0;i<96;++i)printf("%s%u",i?",":"",compiled.uniform_used[i]);
            printf("]");
        }
        if(cpu==CTR_SHADER_OK) {
            printf(",\"written\":[");
            for(unsigned i=0;i<16;++i)printf("%s%u",i?",":"",output.written[i]);
            printf("],\"outputs\":[");
            for(unsigned r=0;r<16;++r)for(unsigned c=0;c<4;++c) {
                uint32_t value;memcpy(&value,&output.values[r][c],4);
                printf("%s%" PRIu32,(r || c)?",":"",value);
            }
            printf("]");
        }
        puts("}");
    }
    return ferror(stdin) || ferror(stdout)?6:0;
}
