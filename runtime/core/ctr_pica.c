/* Original bounded no-draw PICA command front end. See ctr_pica.h for scope. */
#include "ctr_pica.h"
#include <string.h>

static uint32_t little_word(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static bool valid_span(const void *p,size_t size) {
    return !size || !p || (uintptr_t)p<=UINTPTR_MAX-size;
}
static bool overlap(const void *a,size_t as,const void *b,size_t bs) {
    return as && bs && (uintptr_t)a<(uintptr_t)b+bs && (uintptr_t)b<(uintptr_t)a+as;
}
static ctr_pica_result publish(ctr_pica_report *out,ctr_pica_report report,ctr_pica_result result) {
    report.result=result;if(out)*out=report;return result;
}
static ctr_pica_result arguments(const void *bytes,size_t size,
                                ctr_pica_write *writes,size_t capacity,
                                ctr_pica_report *report) {
    if(!valid_span(bytes,size) || (report && !valid_span(report,sizeof(*report))))return CTR_PICA_OVERFLOW;
    if(capacity>SIZE_MAX/sizeof(*writes))return CTR_PICA_OVERFLOW;
    size_t output_size=capacity*sizeof(*writes);
    if(writes && !valid_span(writes,output_size))return CTR_PICA_OVERFLOW;
    if((report && overlap(bytes,size,report,sizeof(*report))) ||
       (writes && overlap(bytes,size,writes,output_size)) ||
       (writes && report && overlap(writes,output_size,report,sizeof(*report))))return CTR_PICA_ALIAS;
    if((!bytes && size) || (!writes && capacity))return CTR_PICA_ARGUMENT;
    return CTR_PICA_OK;
}
/* Validate every record before inspection output or executable state changes. */
static ctr_pica_result validate(const uint8_t *bytes,size_t size,size_t max_writes,
                                ctr_pica_report *report) {
    if(size>CTR_PICA_MAX_BYTES || max_writes>CTR_PICA_MAX_WRITES)return CTR_PICA_LIMIT;
    if(size&7u)return CTR_PICA_ALIGNMENT;
    size_t offset=0;
    while(offset<size) {
        uint32_t value=little_word(bytes+offset),header=little_word(bytes+offset+4);
        unsigned reg=header&65535u;
        size_t count=1u+((header>>20)&255u);
        bool increment=(header>>31)!=0;
        report->byte_offset=offset;report->parameter_offset=offset;
        report->header=header;report->value=value;report->reg=(uint16_t)reg;
        if(header&UINT32_C(0x70000000))return CTR_PICA_HEADER;
        /* Low sixteen bits form the identifier; high identifiers are not
         * silently masked into a different PICA register. */
        if(reg>=CTR_PICA_REGISTER_COUNT || (increment && count>CTR_PICA_REGISTER_COUNT-reg))return CTR_PICA_RANGE;
        size_t record_size=(count+1u)*4u;
        record_size=(record_size+7u)&~(size_t)7u;
        if(record_size>size-offset)return CTR_PICA_TRUNCATED;
        if(count>max_writes-report->writes)return CTR_PICA_LIMIT;
        report->writes+=count;++report->records;offset+=record_size;
    }
    report->byte_offset=size;report->parameter_offset=size;
    report->header=0;report->value=0;report->reg=0;
    return CTR_PICA_OK;
}
uint32_t ctr_pica_merge(uint32_t previous,uint32_t value,unsigned byte_mask) {
    uint32_t mask=0;for(unsigned i=0;i<4;++i)if(byte_mask&(1u<<i))mask|=UINT32_C(255)<<(i*8u);
    return (previous&~mask)|(value&mask);
}
ctr_pica_result ctr_pica_inspect(const void *bytes,size_t size,size_t max_writes,
                               ctr_pica_write *writes,size_t capacity,
                               ctr_pica_report *report) {
    ctr_pica_result result=arguments(bytes,size,writes,capacity,report);
    /* Aliased/overflowed argument ranges cannot safely receive a report. */
    if(result==CTR_PICA_ALIAS || result==CTR_PICA_OVERFLOW)return result;
    ctr_pica_report checked={0};
    if(result!=CTR_PICA_OK)return publish(report,checked,result);
    result=validate(bytes,size,max_writes,&checked);
    if(result!=CTR_PICA_OK)return publish(report,checked,result);
    if(writes && checked.writes>capacity)return publish(report,checked,CTR_PICA_CAPACITY);
    const uint8_t *data=bytes;size_t offset=0,index=0;
    if(writes)while(offset<size) {
        uint32_t header=little_word(data+offset+4);unsigned reg=header&65535u;
        size_t count=1u+((header>>20)&255u);
        for(size_t i=0;i<count;++i) {
            size_t at=i?offset+4u+i*4u:offset;
            ctr_pica_write write={.byte_offset=offset,.value=little_word(data+at),
                .reg=(uint16_t)(reg+((header>>31)?i:0)),.byte_mask=(uint8_t)((header>>16)&15u)};
            writes[index++]=write;
        }
        offset+=(((count+1u)*4u)+7u)&~(size_t)7u;
    }
    return publish(report,checked,CTR_PICA_OK);
}
void ctr_pica_state_init(ctr_pica_state *state) { if(state)memset(state,0,sizeof(*state)); }
bool ctr_pica_register_supported(unsigned reg) {
    /* Ordinary rasterizer configuration, not triggers, streams or unknowns.
     * Names/addresses are documented by libctru's GPU register definitions and
     * 3dbrew GPU/Internal Registers; finalization is separately handled below. */
    return (reg>=0x041u && reg<=0x044u) || (reg>=0x065u && reg<=0x068u);
}
ctr_pica_result ctr_pica_execute(ctr_pica_state *state,const void *bytes,
                               size_t size,size_t max_writes,ctr_pica_report *report) {
    ctr_pica_result result=arguments(bytes,size,NULL,0,report);
    if(result==CTR_PICA_ALIAS || result==CTR_PICA_OVERFLOW)return result;
    ctr_pica_report checked={0};
    if(!state)return publish(report,checked,CTR_PICA_ARGUMENT);
    if(!valid_span(state,sizeof(*state)))return CTR_PICA_OVERFLOW;
    if(overlap(bytes,size,state,sizeof(*state)) ||
       (report && overlap(report,sizeof(*report),state,sizeof(*state))))return CTR_PICA_ALIAS;
    if(result!=CTR_PICA_OK)return publish(report,checked,result);
    result=validate(bytes,size,max_writes,&checked);
    if(result!=CTR_PICA_OK)return publish(report,checked,result);
    if(size&15u)return publish(report,checked,CTR_PICA_ALIGNMENT);
    const uint8_t *data=bytes;size_t offset=0,finalizations=0;
    while(offset<size) {
        uint32_t header=little_word(data+offset+4);unsigned reg=header&65535u,mask=(header>>16)&15u;
        size_t count=1u+((header>>20)&255u);
        for(size_t i=0;i<count;++i) {
            unsigned current=reg+(unsigned)((header>>31)?i:0);size_t at=i?offset+4u+i*4u:offset;
            uint32_t value=little_word(data+at);
            checked.byte_offset=offset;checked.parameter_offset=at;checked.header=header;
            checked.reg=(uint16_t)current;checked.value=value;
            if(current==CTR_PICA_FINALIZE) {
                /* Exact documented libctru end marker. No previous draw work
                 * exists in this subset, and the marker never stores a value. */
                if(mask!=15u || value!=CTR_PICA_FINALIZE_VALUE || finalizations==2u || count!=1u)
                    return publish(report,checked,CTR_PICA_FINALIZATION);
                ++finalizations;
            } else {
                if(!ctr_pica_register_supported(current))return publish(report,checked,CTR_PICA_UNSUPPORTED);
                if(finalizations)return publish(report,checked,CTR_PICA_FINALIZATION);
            }
        }
        offset+=(((count+1u)*4u)+7u)&~(size_t)7u;
    }
    if(!finalizations)return publish(report,checked,CTR_PICA_FINALIZATION);
    if(state->committed_lists==UINT64_MAX || checked.writes>UINT64_MAX-state->register_writes ||
       finalizations>UINT64_MAX-state->finalizations)return publish(report,checked,CTR_PICA_OVERFLOW);
    /* Fully preflight before this infallible second pass. Input/state/report
       aliases were rejected above, so committing cannot change unread input.
       Avoid a 4 KiB automatic register snapshot on the Wii U call stack. */
    offset=0;
    while(offset<size) {
        uint32_t header=little_word(data+offset+4);unsigned reg=header&65535u,mask=(header>>16)&15u;
        size_t count=1u+((header>>20)&255u);
        for(size_t i=0;i<count;++i) {
            unsigned current=reg+(unsigned)((header>>31)?i:0);
            if(current!=CTR_PICA_FINALIZE) {
                size_t at=i?offset+4u+i*4u:offset;
                state->registers[current]=ctr_pica_merge(state->registers[current],little_word(data+at),mask);
            }
        }
        offset+=(((count+1u)*4u)+7u)&~(size_t)7u;
    }
    ++state->committed_lists;state->register_writes+=checked.writes;state->finalizations+=finalizations;
    checked.byte_offset=size;checked.parameter_offset=size;
    checked.header=0;checked.value=0;checked.reg=0;checked.finalizations=finalizations;
    return publish(report,checked,CTR_PICA_OK);
}
const char *ctr_pica_result_name(ctr_pica_result result) {
    static const char *const names[]={"ok","argument","alignment","header","register-range",
        "truncated","write-or-byte-limit","output-capacity","alias","unsupported-register",
        "finalization","counter-or-pointer-overflow"};
    return (unsigned)result<sizeof(names)/sizeof(*names)?names[result]:"unknown-result";
}
