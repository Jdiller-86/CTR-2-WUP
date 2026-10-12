#include "ctr_pica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static void put(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static size_t add(uint8_t *bytes,size_t offset,unsigned reg,unsigned mask,
                  bool increment,const uint32_t *values,unsigned count) {
    CHECK(count && count<=256u);
    uint32_t header=reg|(mask<<16)|((count-1u)<<20)|(increment?UINT32_C(0x80000000):0);
    put(bytes+offset,values[0]);put(bytes+offset+4,header);
    for(unsigned i=1;i<count;++i)put(bytes+offset+4+i*4,values[i]);
    size_t length=((count+1u)*4u+7u)&~(size_t)7u;
    /* Unused padding deliberately nonzero; hardware skips it. */
    if((count+1u)&1u)put(bytes+offset+length-4,UINT32_C(0xbad0feed));
    return offset+length;
}
static size_t single(uint8_t *bytes,size_t at,unsigned reg,unsigned mask,uint32_t value) {
    return add(bytes,at,reg,mask,false,&value,1);
}
static size_t finalize(uint8_t *bytes,size_t at) {
    at=single(bytes,at,CTR_PICA_FINALIZE,15,CTR_PICA_FINALIZE_VALUE);
    if(at&15u)at=single(bytes,at,CTR_PICA_FINALIZE,15,CTR_PICA_FINALIZE_VALUE);
    return at;
}
static void inspection(void) {
    uint8_t backing[4097]={0},*bytes=backing+1;uint32_t values[256];
    for(unsigned i=0;i<256;++i)values[i]=UINT32_C(0x0137a000)+i;
    ctr_pica_write writes[512];ctr_pica_report report;
    size_t size=add(bytes,0,0x2ffu,5,true,values,256);
    CHECK(ctr_pica_inspect(bytes,size,256,writes,512,&report)==CTR_PICA_OK);
    CHECK(report.records==1 && report.writes==256 && report.byte_offset==size);
    for(unsigned i=0;i<256;++i) {
        CHECK(writes[i].reg==0x2ffu+i && writes[i].byte_mask==5 && writes[i].byte_offset==0);
        CHECK(writes[i].value==values[i]);
    }
    size=add(bytes,0,0x3ffu,0,false,values,256);
    CHECK(ctr_pica_inspect(bytes,size,256,writes,512,&report)==CTR_PICA_OK);
    for(unsigned i=0;i<256;++i)CHECK(writes[i].reg==0x3ffu && writes[i].byte_mask==0 && writes[i].value==values[i]);
    size_t second=size;size=add(bytes,size,0x65,15,true,values,3);
    CHECK(ctr_pica_inspect(bytes,size,259,writes,512,&report)==CTR_PICA_OK);
    CHECK(report.writes==259 && report.records==2 && writes[256].byte_offset==second);
    CHECK(writes[256].reg==0x65 && writes[258].reg==0x67 && writes[258].value==values[2]);
    CHECK(ctr_pica_inspect(bytes,size,259,NULL,0,&report)==CTR_PICA_OK && report.writes==259);
    CHECK(ctr_pica_inspect(NULL,0,0,NULL,0,&report)==CTR_PICA_OK && !report.writes && !report.records);
    CHECK(ctr_pica_inspect(bytes,0,0,NULL,0,NULL)==CTR_PICA_OK);
    /* Numerical host words and wire bytes are independent, including masks. */
    CHECK(single(bytes,0,0x41,15,UINT32_C(0x12345678))==8);
    CHECK(bytes[0]==0x78 && bytes[1]==0x56 && bytes[2]==0x34 && bytes[3]==0x12);
    CHECK(ctr_pica_inspect(bytes,8,1,writes,512,&report)==CTR_PICA_OK && writes[0].value==UINT32_C(0x12345678));
}
static void masks(void) {
    for(unsigned mask=0;mask<16;++mask) {
        uint32_t actual=ctr_pica_merge(UINT32_C(0x10203040),UINT32_C(0xa1b2c3d4),mask),expected=0;
        for(unsigned lane=0;lane<4;++lane) {
            uint32_t value=(mask&(1u<<lane))?UINT32_C(0xa1b2c3d4):UINT32_C(0x10203040);
            expected|=((value>>(lane*8u))&255u)<<(lane*8u);
        }
        CHECK(actual==expected);
    }
    CHECK(ctr_pica_merge(0,UINT32_MAX,0x10)==0);
}
static void structural_failures(void) {
    uint8_t bytes[64]={0};ctr_pica_write writes[4],before[4];ctr_pica_report report;
    memset(writes,0xa5,sizeof(writes));memcpy(before,writes,sizeof(writes));
    uint32_t values[]={1,2,3};size_t size=add(bytes,0,0x3feu,15,true,values,3);
    CHECK(ctr_pica_inspect(bytes,size,4,writes,4,&report)==CTR_PICA_RANGE && report.reg==0x3fe && !report.writes);
    size=add(bytes,0,0x400u,15,false,values,1);
    CHECK(ctr_pica_inspect(bytes,size,4,writes,4,&report)==CTR_PICA_RANGE);
    size=add(bytes,0,0x1041u,15,false,values,1);
    CHECK(ctr_pica_inspect(bytes,size,4,writes,4,&report)==CTR_PICA_RANGE && report.reg==0x1041);
    for(unsigned bit=28;bit<31;++bit) {
        size=single(bytes,0,0x41,15,1);put(bytes+4,(UINT32_C(1)<<bit)|UINT32_C(0x000f0041));
        CHECK(ctr_pica_inspect(bytes,size,4,writes,4,&report)==CTR_PICA_HEADER && !report.writes);
    }
    size=add(bytes,0,0x41,15,true,values,3);
    CHECK(ctr_pica_inspect(bytes,size-1,4,writes,4,&report)==CTR_PICA_ALIGNMENT);
    CHECK(ctr_pica_inspect(bytes,8,4,writes,4,&report)==CTR_PICA_TRUNCATED);
    /* Missing padding is distinct from parameter truncation. */
    size=add(bytes,0,0x41,15,true,values,2);
    CHECK(size==16 && ctr_pica_inspect(bytes,8,4,writes,4,&report)==CTR_PICA_TRUNCATED);
    CHECK(ctr_pica_inspect(bytes,size,1,writes,4,&report)==CTR_PICA_LIMIT);
    CHECK(ctr_pica_inspect(bytes,size,4,writes,1,&report)==CTR_PICA_CAPACITY && report.writes==2);
    CHECK(ctr_pica_inspect(bytes,size,4,NULL,1,&report)==CTR_PICA_ARGUMENT);
    CHECK(ctr_pica_inspect(NULL,8,4,writes,4,&report)==CTR_PICA_ARGUMENT);
    /* Give the API an actual disjoint over-limit span, rather than claiming
       a multi-megabyte stack span that also aliases the outputs. */
    uint8_t *oversized=malloc(CTR_PICA_MAX_BYTES+8u);CHECK(oversized);
    CHECK(ctr_pica_inspect(oversized,CTR_PICA_MAX_BYTES+8u,4,writes,4,&report)==CTR_PICA_LIMIT);
    free(oversized);
    CHECK(ctr_pica_inspect(bytes,size,CTR_PICA_MAX_WRITES+1u,writes,4,&report)==CTR_PICA_LIMIT);
    CHECK(!memcmp(writes,before,sizeof(writes)));
    /* Valid prefix output must not leak when a later record is invalid. */
    size=single(bytes,0,0x41,15,1);size=single(bytes,size,0x400,15,2);
    CHECK(ctr_pica_inspect(bytes,size,4,writes,4,&report)==CTR_PICA_RANGE && report.byte_offset==8 && report.writes==1);
    CHECK(!memcmp(writes,before,sizeof(writes)));
}
static void limits(void) {
    const size_t capacity=CTR_PICA_MAX_WRITES+1u;
    uint8_t *bytes=malloc(capacity*8u);CHECK(bytes);
    size_t size=0;for(size_t i=0;i<capacity;++i)size=single(bytes,size,0x41,1,(uint32_t)i);
    ctr_pica_report report;
    CHECK(ctr_pica_inspect(bytes,size-8u,CTR_PICA_MAX_WRITES,NULL,0,&report)==CTR_PICA_OK && report.writes==CTR_PICA_MAX_WRITES);
    CHECK(ctr_pica_inspect(bytes,size,CTR_PICA_MAX_WRITES,NULL,0,&report)==CTR_PICA_LIMIT && report.writes==CTR_PICA_MAX_WRITES);
    CHECK(report.byte_offset==size-8u);free(bytes);
}
static void aliasing(void) {
    union { uint8_t bytes[4096];ctr_pica_report report;ctr_pica_write writes[8]; } storage;
    uint8_t before[4096];memset(&storage,0xa5,sizeof(storage));single(storage.bytes,0,0x41,15,1);
    memcpy(before,&storage,sizeof(storage));ctr_pica_report report;memset(&report,0x5a,sizeof(report));
    ctr_pica_report saved=report;
    CHECK(ctr_pica_inspect(storage.bytes,8,1,storage.writes,8,&report)==CTR_PICA_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)) && !memcmp(&saved,&report,sizeof(report)));
    CHECK(ctr_pica_inspect(storage.bytes,8,1,NULL,0,&storage.report)==CTR_PICA_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    uint8_t bytes[16];finalize(bytes,0);
    CHECK(ctr_pica_inspect(bytes,16,4,storage.writes,8,&storage.report)==CTR_PICA_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    CHECK(ctr_pica_inspect(bytes,16,4,storage.writes,SIZE_MAX,&report)==CTR_PICA_OVERFLOW);
    CHECK(!memcmp(before,&storage,sizeof(storage)) && !memcmp(&saved,&report,sizeof(report)));
    CHECK(ctr_pica_inspect((const void *)(UINTPTR_MAX-3u),8,4,NULL,0,&report)==CTR_PICA_OVERFLOW);
    CHECK(!memcmp(&saved,&report,sizeof(report)));
}
static void execution(void) {
    uint8_t bytes[256]={0};ctr_pica_state state;ctr_pica_state_init(&state);ctr_pica_report report;
    uint32_t values[]={UINT32_C(0x01020304),UINT32_C(0xaabbccdd),UINT32_C(0x99aabbcc),UINT32_C(0xeeddccbb)};
    size_t size=add(bytes,0,0x41,15,true,values,4);size=finalize(bytes,size);
    CHECK(size==32 && ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_OK);
    CHECK(state.committed_lists==1 && state.register_writes==5 && state.finalizations==1 && report.finalizations==1);
    for(unsigned i=0;i<4;++i)CHECK(state.registers[0x41+i]==values[i]);
    CHECK(!state.registers[CTR_PICA_FINALIZE]);
    size=single(bytes,0,0x42,5,UINT32_C(0x12345678));size=single(bytes,size,0x43,0,0);
    size=add(bytes,size,0x65,15,true,values,4);size=finalize(bytes,size);
    CHECK(size==48 && ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_OK);
    CHECK(state.registers[0x42]==UINT32_C(0xaa34cc78) && state.registers[0x43]==values[2]);
    for(unsigned i=0;i<4;++i)CHECK(state.registers[0x65+i]==values[i]);
    /* Two single writes + four grouped writes + one tail marker: the payload
       is forty bytes, so a single marker reaches sixteen-byte alignment. */
    CHECK(state.committed_lists==2 && state.register_writes==12 && state.finalizations==2 && report.finalizations==1);
    size=finalize(bytes,0);CHECK(size==16);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_OK && state.committed_lists==3 && state.finalizations==4);
    for(unsigned reg=0;reg<CTR_PICA_REGISTER_COUNT;++reg) {
        bool expected=(reg>=0x41 && reg<=0x44)||(reg>=0x65 && reg<=0x68);
        CHECK(ctr_pica_register_supported(reg)==expected);
    }
    CHECK(!ctr_pica_register_supported(CTR_PICA_REGISTER_COUNT));
    CHECK(!ctr_pica_register_supported(UINT32_MAX));
}
static void rejected_execution(void) {
    ctr_pica_state state;ctr_pica_state_init(&state);state.registers[0x41]=UINT32_C(0xdeadbeef);
    ctr_pica_state before=state;uint8_t bytes[128]={0};ctr_pica_report report;size_t size;
    const unsigned blocked[]={0,0x11,0x40,0x4d,0x63,0x80,0xb1,0x1c5,0x227,0x22d,0x22f,0x23c,0x23d,0x290,0x2b0,0x300,0x3ff};
    for(size_t i=0;i<sizeof(blocked)/sizeof(*blocked);++i)for(unsigned mask=0;mask<=15;mask+=15) {
        size=single(bytes,0,0x41,15,7);size=single(bytes,size,blocked[i],mask,1);size=finalize(bytes,size);
        CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_UNSUPPORTED);
        CHECK(report.reg==blocked[i] && report.byte_offset==8 && report.parameter_offset==8);
        CHECK(!memcmp(&state,&before,sizeof(state)));
    }
    CHECK(ctr_pica_execute(&state,NULL,0,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,0x41,15,1);size=single(bytes,size,0x42,15,2);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,CTR_PICA_FINALIZE,15,CTR_PICA_FINALIZE_VALUE);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_ALIGNMENT && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,CTR_PICA_FINALIZE,7,CTR_PICA_FINALIZE_VALUE);size=finalize(bytes,size);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,CTR_PICA_FINALIZE,15,UINT32_C(0x12345679));size=finalize(bytes,size);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,CTR_PICA_FINALIZE,15,CTR_PICA_FINALIZE_VALUE);size=single(bytes,size,0x41,15,1);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=single(bytes,0,0x41,15,1);
    for(unsigned i=0;i<3;++i)size=single(bytes,size,CTR_PICA_FINALIZE,15,CTR_PICA_FINALIZE_VALUE);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    uint32_t values[]={CTR_PICA_FINALIZE_VALUE,CTR_PICA_FINALIZE_VALUE};size=add(bytes,0,CTR_PICA_FINALIZE,15,false,values,2);
    CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_FINALIZATION && !memcmp(&state,&before,sizeof(state)));
    size=finalize(bytes,0);
    CHECK(ctr_pica_execute(&state,bytes,size,1,&report)==CTR_PICA_LIMIT && !memcmp(&state,&before,sizeof(state)));
    for(unsigned counter=0;counter<3;++counter) {
        state=before;if(counter==0)state.committed_lists=UINT64_MAX;
        if(counter==1)state.register_writes=UINT64_MAX;
        if(counter==2)state.finalizations=UINT64_MAX-1;
        ctr_pica_state limit=state;
        CHECK(ctr_pica_execute(&state,bytes,size,16,&report)==CTR_PICA_OVERFLOW && !memcmp(&state,&limit,sizeof(state)));
    }
    state=before;CHECK(ctr_pica_execute(&state,&state,16,16,&report)==CTR_PICA_ALIAS && !memcmp(&state,&before,sizeof(state)));
    CHECK(ctr_pica_execute(&state,bytes,size,16,(ctr_pica_report *)&state)==CTR_PICA_ALIAS && !memcmp(&state,&before,sizeof(state)));
    CHECK(ctr_pica_execute(NULL,bytes,size,16,&report)==CTR_PICA_ARGUMENT);
    CHECK(ctr_pica_execute(&state,bytes,size,16,NULL)==CTR_PICA_OK);
}
int main(void) {
    inspection();masks();structural_failures();limits();aliasing();execution();rejected_execution();
    CHECK(!strcmp(ctr_pica_result_name(CTR_PICA_UNSUPPORTED),"unsupported-register"));
    CHECK(!strcmp(ctr_pica_result_name((ctr_pica_result)100),"unknown-result"));
    printf("PICA command inspection/no-draw state: %u assertions passed\n",assertions);return 0;
}
