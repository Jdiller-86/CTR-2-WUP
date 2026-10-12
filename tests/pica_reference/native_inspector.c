/* Original byte-stream adapter for PICA inspection, never a draw backend. */
#include "ctr_pica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    if(argc!=3) return 2;
    char *end;
    unsigned long limit=strtoul(argv[1],&end,0);
    if(*end || limit>CTR_PICA_MAX_WRITES+1u) return 2;
    bool count_only=strcmp(argv[2],"count-only")==0;
    unsigned long capacity=count_only ? 0 : strtoul(argv[2],&end,0);
    if(!count_only && (*end || capacity>CTR_PICA_MAX_WRITES)) return 2;
    unsigned char *bytes=malloc(CTR_PICA_MAX_BYTES+8);
    ctr_pica_write *writes=count_only ? NULL : malloc((capacity ? capacity : 1)*sizeof(*writes));
    if(!bytes || (!count_only && !writes)) { free(bytes);free(writes);return 3; }
    size_t size=fread(bytes,1,CTR_PICA_MAX_BYTES+8,stdin);
    if(ferror(stdin) || fgetc(stdin)!=EOF) { free(bytes);free(writes);return 4; }
    size_t output_bytes=capacity*sizeof(*writes);
    if(writes) memset(writes,0xa5,output_bytes);
    ctr_pica_report report={0};
    ctr_pica_result result=ctr_pica_inspect(bytes,size,limit,writes,capacity,&report);
    bool unchanged=true;
    if(result!=CTR_PICA_OK && writes) for(size_t i=0;i<output_bytes;++i)
        if(((const unsigned char *)writes)[i]!=0xa5) unchanged=false;
    printf("{\"result\":\"%s\",\"error_offset\":%zu,\"records\":%zu,"
           "\"write_count\":%zu,\"output_unchanged_on_error\":%s,\"writes\":[",
           ctr_pica_result_name(result),report.byte_offset,report.records,report.writes,
           unchanged ? "true" : "false");
    if(result==CTR_PICA_OK && writes) for(size_t i=0;i<report.writes;++i) {
        ctr_pica_write w=writes[i];
        printf("%s[%zu,%u,%u,%u]",i ? "," : "",w.byte_offset,(unsigned)w.reg,
               (unsigned)w.byte_mask,(unsigned)w.value);
    }
    printf("],\"merged\":[");
    if(result==CTR_PICA_OK && writes) for(size_t i=0;i<report.writes;++i)
        printf("%s%u",i ? "," : "",(unsigned)ctr_pica_merge(UINT32_C(0x12345678),
                    writes[i].value,writes[i].byte_mask));
    ctr_pica_state state,before;
    ctr_pica_state_init(&state);before=state;
    ctr_pica_report state_report={0};
    ctr_pica_result state_result=ctr_pica_execute(&state,bytes,size,limit,&state_report);
    printf("],\"state_result\":\"%s\",\"state_unchanged_on_error\":%s,"
           "\"state_committed_lists\":%llu,\"state_register_writes\":%llu,"
           "\"state_finalizations\":%llu,\"state_registers\":[",
           ctr_pica_result_name(state_result),
           state_result==CTR_PICA_OK || memcmp(&state,&before,sizeof(state))==0 ? "true" : "false",
           (unsigned long long)state.committed_lists,
           (unsigned long long)state.register_writes,
           (unsigned long long)state.finalizations);
    const unsigned registers[]={0x41,0x42,0x43,0x44,0x65,0x66,0x67,0x68};
    for(size_t i=0;i<sizeof(registers)/sizeof(registers[0]);++i)
        printf("%s%u",i ? "," : "",(unsigned)state.registers[registers[i]]);
    puts("]}");
    free(bytes);free(writes);return 0;
}
