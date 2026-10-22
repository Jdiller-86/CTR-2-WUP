#include "ctr_gx2_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static ctr_shader_program p;
static ctr_gx2_shader out,previous;
static uint32_t maps[7];
static uint32_t get32(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|(uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}
static unsigned bit_count(unsigned value) {
    unsigned count=0;while(value) { count+=value&1u;value>>=1; }return count;
}
static uint32_t mov(unsigned source,unsigned dest,unsigned descriptor) {
    return UINT32_C(0x4c000000)|(source<<12)|(dest<<21)|descriptor;
}
static uint32_t binary(unsigned opcode,unsigned a,unsigned b,unsigned dest,unsigned descriptor) {
    return (opcode<<26)|(a<<12)|(b<<7)|(dest<<21)|descriptor;
}
static void init(void) {
    memset(&p,0,sizeof(p));memset(maps,0,sizeof(maps));
    p.code[0]=mov(0,0,0);p.code[1]=mov(1,1,0);p.code[2]=UINT32_C(0x88000000);
    p.code_defined[0]=p.code_defined[1]=p.code_defined[2]=p.descriptor_defined[0]=1;
    p.descriptors[0]=UINT32_C(0x36f);
    maps[0]=UINT32_C(0x03020100);maps[1]=UINT32_C(0x0b0a0908);
}
static ctr_gx2_shader_result run(void) { return ctr_gx2_shader_compile(&p,3,maps,2,&out); }
static void mapped_failure(uint16_t mask,unsigned count,ctr_gx2_shader_result expected) {
    memset(&out,0xa5,sizeof(out));previous=out;
    CHECK(ctr_gx2_shader_compile(&p,mask,maps,count,&out)==expected);
    CHECK(!memcmp(&out,&previous,sizeof(out)));
}
static void failure(ctr_gx2_shader_result expected) { mapped_failure(3,2,expected); }
static void original_shader(void) {
    init();CHECK(run()==CTR_GX2_SHADER_OK);
    CHECK(out.gprs==53 && !out.texcoord0 && out.input_mask==3 && out.pica_instructions==3 && out.alu_instructions==24);
    CHECK(out.size==768u);
    CHECK(get32(out.program)==0 && get32(out.program+4)==UINT32_C(0x09800000));
    CHECK(get32(out.program+8)==64 && get32(out.program+12)==UINT32_C(0xa05c0000));
    /* Same published WUT sample instructions, with our explicit output GPRs.
     * POS0,R51.xyzw then PARAM0,R52.xyzw, END_OF_PROGRAM on PARAM0. */
    CHECK(get32(out.program+16)==UINT32_C(0x0019a03c));
    CHECK(get32(out.program+20)==UINT32_C(0x94000688));
    CHECK(get32(out.program+24)==UINT32_C(0x001a4000));
    CHECK(get32(out.program+28)==UINT32_C(0x14200688));
    CHECK(out.program[4]==0 && out.program[5]==0 && out.program[6]==0x80 && out.program[7]==9);
    for(unsigned i=32;i<512;++i)CHECK(out.program[i]==0);
    for(unsigned i=512u+24u*8u;i<CTR_GX2_SHADER_MAX_BYTES;++i)CHECK(out.program[i]==0);
    /* First capture is MOV R49.x,R1.x, LAST; this does not execute a PICA
     * source through a CPU-produced post-shader vertex buffer. */
    CHECK(get32(out.program+512)==UINT32_C(0x80000001));
    CHECK(get32(out.program+516)==UINT32_C(0x06200c90));
    for(unsigned u=0;u<96;++u)CHECK(!out.uniform_used[u]);
    CHECK(out.uniform_count==0);
}
static void inputs_uniforms_and_dependencies(void) {
    init();p.code[0]=mov(15,0,0);p.code[1]=mov(32+95,1,0);
    p.uniform_defined[95]=15;
    for(unsigned lane=0;lane<4;++lane)p.uniforms[95][lane]=UINT32_C(0x3f800000);
    CHECK(run()==CTR_GX2_SHADER_OK && out.input_mask==(1u<<15) && out.uniform_used[95]==15);
    CHECK((get32(out.program+512)&511u)==16);
    CHECK((get32(out.program+512+8u*8u)&511u)==351);
    p.uniform_defined[95]=7;failure(CTR_GX2_SHADER_UNDEFINED);
    p.uniform_defined[95]=15;p.uniforms[95][3]=1;failure(CTR_GX2_SHADER_NUMERIC);
    init();p.code[0]=mov(16,0,0);failure(CTR_GX2_SHADER_UNDEFINED);
    /* A masked write defines only its own lane, and subsequent source reads
     * must not accidentally claim the rest of that temporary. */
    p.code[0]=mov(0,16,1);p.descriptor_defined[1]=1;p.descriptors[1]=8;
    p.code[1]=mov(16,0,0);failure(CTR_GX2_SHADER_UNDEFINED);
    p.code[1]=mov(16,0,1);p.code[2]=mov(0,0,0);p.code_defined[3]=1;
    p.code[3]=mov(1,1,0);p.code_defined[4]=1;p.code[4]=UINT32_C(0x88000000);
    CHECK(run()==CTR_GX2_SHADER_OK);
    /* All inputs, all temporary destinations and both binary encodings.
     * Source-capture and destination instructions occupy separate groups. */
    for(unsigned input=0;input<16;++input)for(unsigned temp=0;temp<16;++temp)
        for(unsigned op=0;op<=8;op+=8) {
            init();p.code[0]=mov(input,16u+temp,0);p.code[1]=binary(op,16u+temp,16u+temp,0,1);
            p.code[2]=mov(1,1,0);p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=p.descriptor_defined[1]=1;
            p.descriptors[1]=UINT32_C(0x36f)|(228u<<14);
            CHECK(run()==CTR_GX2_SHADER_OK);
            unsigned start=512u+8u*8u;
            CHECK((get32(out.program+start)&511u)==17u+temp);
            CHECK((get32(out.program+start+4u*8u)&511u)==17u+temp);
            CHECK(((get32(out.program+start+8u*8u+4u)>>7)&2047u)==(op?2u:0u));
        }
}
static void swizzles_and_masks(void) {
    /* Exhaust every first-operand swizzle, sign and destination mask. A second
     * unmasked MOV supplies all exported lanes, including mask-zero cases. */
    for(unsigned swizzle=0;swizzle<256;++swizzle)for(unsigned sign=0;sign<2;++sign)
        for(unsigned mask=0;mask<16;++mask) {
            init();p.code[0]=mov(0,16,1);p.code[1]=mov(0,0,0);p.code[2]=mov(1,1,0);
            p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=p.descriptor_defined[1]=1;
            p.descriptors[1]=mask|(sign<<4)|(swizzle<<5);
            CHECK(run()==CTR_GX2_SHADER_OK);
            unsigned slot=0;
            for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
                uint32_t instruction=get32(out.program+512u+slot++*8u);
                CHECK(((instruction>>10)&3u)==((swizzle>>(6u-lane*2u))&3u));
                CHECK(((instruction>>12)&1u)==sign && (instruction&UINT32_C(0x80000000)));
            }
            CHECK(out.alu_instructions==24u+bit_count(mask)*2u);
        }
}
static void semantic_maps(void) {
    init();maps[0]=UINT32_C(0x03090900);failure(CTR_GX2_SHADER_UNDEFINED);
    init();maps[0]=UINT32_C(0x03020104);failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();maps[0]=UINT32_C(0x03020180);failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();CHECK(ctr_gx2_shader_compile(&p,3,maps,1,&out)==CTR_GX2_SHADER_UNSUPPORTED);
    CHECK(ctr_gx2_shader_compile(&p,0,maps,0,&out)==CTR_GX2_SHADER_UNSUPPORTED);
    /* Semantics span arbitrary output registers and arbitrary source lanes. */
    init();p.code[2]=mov(2,15,0);p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=1;
    maps[0]=UINT32_C(0x031f0900);maps[1]=UINT32_C(0x0b020108);maps[2]=UINT32_C(0x1f1f1f0a);
    CHECK(ctr_gx2_shader_compile(&p,UINT16_C(0x8003),maps,3,&out)==CTR_GX2_SHADER_OK);
    CHECK(out.input_mask==7 && out.alu_instructions==32);
}
static void texture_coordinate_exports(void) {
    init();p.code[2]=mov(2,2,0);p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=1;
    maps[2]=UINT32_C(0x1f1f0d0c);
    CHECK(ctr_gx2_shader_compile(&p,7,maps,3,&out)==CTR_GX2_SHADER_OK);
    CHECK(out.texcoord0 && out.gprs==54 && out.input_mask==7);
    CHECK(out.alu_instructions==34 && out.pica_instructions==4);
    CHECK(get32(out.program+20)==UINT32_C(0x94000688));
    CHECK(get32(out.program+28)==UINT32_C(0x13800688));
    CHECK(get32(out.program+32)==(1u|(2u<<13)|(53u<<15)));
    CHECK(get32(out.program+36)==UINT32_C(0x14200b08));
    for(unsigned lane=0;lane<2;++lane) {
        unsigned at=512u+(32u+lane)*8u;
        CHECK((get32(out.program+at)&511u)==35u);
        CHECK(((get32(out.program+at)>>10)&3u)==lane);
        CHECK(((get32(out.program+at+4)>>21)&127u)==53u);
        CHECK((get32(out.program+at+4)>>29)==lane);
    }
    /* UV comes from any two written lanes, independently of storage order. */
    for(unsigned u=0;u<4;++u)for(unsigned v=0;v<4;++v)if(u!=v) {
        maps[2]=UINT32_C(0x1f1f1f1f);
        maps[2]=(maps[2]&~(255u<<(8u*u)))|(12u<<(8u*u));
        maps[2]=(maps[2]&~(255u<<(8u*v)))|(13u<<(8u*v));
        CHECK(ctr_gx2_shader_compile(&p,7,maps,3,&out)==CTR_GX2_SHADER_OK);
        for(unsigned index=0;index<2;++index) {
            unsigned lane=index==0?(u<v?u:v):(u<v?v:u);
            unsigned at=512u+(32u+index)*8u;
            CHECK(((get32(out.program+at)>>10)&3u)==lane);
            CHECK((get32(out.program+at+4)>>29)==(lane==u?0u:1u));
        }
    }
    maps[2]=UINT32_C(0x1f1f1f0c);mapped_failure(7,3,CTR_GX2_SHADER_UNDEFINED);
    maps[2]=UINT32_C(0x1f1f1f0d);mapped_failure(7,3,CTR_GX2_SHADER_UNDEFINED);
    maps[2]=UINT32_C(0x1f1f0c0c);mapped_failure(7,3,CTR_GX2_SHADER_UNDEFINED);
    maps[2]=UINT32_C(0x1f0e0d0c);mapped_failure(7,3,CTR_GX2_SHADER_UNSUPPORTED);
    maps[2]=UINT32_C(0x1f1f0d0c);
    p.code[2]=mov(2,2,1);p.descriptors[1]=8;p.descriptor_defined[1]=1;
    mapped_failure(7,3,CTR_GX2_SHADER_UNDEFINED);
    /* Declared UV semantics do not excuse an absent required color lane. */
    p.code[2]=mov(2,2,0);maps[1]=UINT32_C(0x1f0a0908);
    mapped_failure(7,3,CTR_GX2_SHADER_UNDEFINED);
    /* A prior UV result cannot leave its metadata set on an untextured compile. */
    init();CHECK(run()==CTR_GX2_SHADER_OK && !out.texcoord0 && out.gprs==53);
}
static void revalidation_matches(ctr_gx2_shader_result expected) {
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&previous)==expected);
    out=previous;
    CHECK(run()==expected);
    /* Current constant values never change generated code or its metadata,
     * and a failed recompile must leave the prior output intact. */
    CHECK(!memcmp(&out,&previous,sizeof(out)));
}
static void uniform_revalidation(void) {
    /* Encounter order differs from both uniform and lane index order. A later
     * repeat must not add another dependency or change error precedence. */
    init();p.code[0]=mov(127,0,1);p.code[1]=mov(34,1,0);p.code[2]=mov(127,16,0);
    p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=p.descriptor_defined[1]=1;
    p.descriptors[1]=15u|(228u<<5);
    p.uniform_defined[95]=p.uniform_defined[2]=15;
    for(unsigned lane=0;lane<4;++lane) {
        p.uniforms[95][lane]=UINT32_C(0x3f800000);
        p.uniforms[2][lane]=UINT32_C(0x40000000);
    }
    CHECK(run()==CTR_GX2_SHADER_OK);previous=out;
    CHECK(previous.uniform_count==8 && previous.uniform_used[95]==15 && previous.uniform_used[2]==15);
    for(unsigned lane=0;lane<4;++lane) {
        CHECK(previous.uniform_order[lane]==383u-lane);
        CHECK(previous.uniform_order[4u+lane]==8u+lane);
    }
    revalidation_matches(CTR_GX2_SHADER_OK);
    p.uniforms[95][3]=UINT32_C(0x7fc00000);p.uniform_defined[95]=14;p.uniform_defined[2]=14;
    revalidation_matches(CTR_GX2_SHADER_NUMERIC);
    p.uniform_defined[95]=7;p.uniforms[95][3]=UINT32_C(0x3f800000);
    p.uniforms[95][0]=1;p.uniforms[2][0]=UINT32_C(0x7f800000);
    revalidation_matches(CTR_GX2_SHADER_UNDEFINED);
    p.uniform_defined[95]=p.uniform_defined[2]=15;
    for(unsigned lane=0;lane<4;++lane) {
        p.uniforms[95][lane]=UINT32_C(0x3f800000);
        p.uniforms[2][lane]=UINT32_C(0x40000000);
    }
    static const struct { uint32_t bits;bool exact; } values[]={
        {0,true},{UINT32_C(0x80000000),true},{UINT32_C(0xbf800000),true},
        {UINT32_C(0x3f800000),true},{1,false},{UINT32_C(0x7f800000),false},
        {UINT32_C(0x7fc00000),false},{UINT32_C(0x3f800001),false}
    };
    for(unsigned i=0;i<previous.uniform_count;++i) {
        unsigned slot=previous.uniform_order[i],u=slot/4u,lane=slot%4u;
        uint32_t original=p.uniforms[u][lane];
        for(unsigned value=0;value<sizeof(values)/sizeof(values[0]);++value) {
            p.uniforms[u][lane]=values[value].bits;
            revalidation_matches(values[value].exact?CTR_GX2_SHADER_OK:CTR_GX2_SHADER_NUMERIC);
        }
        p.uniforms[u][lane]=original;
    }
    static ctr_shader_program program_before;
    static ctr_gx2_shader compiled_before;
    program_before=p;compiled_before=previous;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&previous)==CTR_GX2_SHADER_OK);
    CHECK(!memcmp(&p,&program_before,sizeof(p)) && !memcmp(&previous,&compiled_before,sizeof(previous)));
    /* Reject a malformed complete record before examining current values. */
    p.uniform_defined[95]=0;
    out=previous;out.uniform_count=CTR_SHADER_UNIFORMS*4u+1u;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_order[0]=CTR_SHADER_UNIFORMS*4u;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_order[1]=out.uniform_order[0];
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_used[95]|=16;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_used[95]=7;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_count=0;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    out=previous;out.uniform_used[0]=1;
    CHECK(ctr_gx2_shader_validate_uniforms(&p,&out)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_validate_uniforms(NULL,&previous)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_validate_uniforms(&p,NULL)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_validate_uniforms((const ctr_shader_program *)((const uint8_t *)&p+1),&previous)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_validate_uniforms(&p,(const ctr_gx2_shader *)((const uint8_t *)&previous+1))==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_validate_uniforms((const ctr_shader_program *)(uintptr_t)(UINTPTR_MAX-3u),&previous)==CTR_GX2_SHADER_OVERFLOW);
    CHECK(ctr_gx2_shader_validate_uniforms(&p,(const ctr_gx2_shader *)(uintptr_t)(UINTPTR_MAX-3u))==CTR_GX2_SHADER_OVERFLOW);
    /* Repeated swizzled reads record one lane. All unused constants may be
     * undefined or nonnumeric; positive and negative zero remain admissible. */
    init();p.code[0]=mov(127,16,1);p.code[1]=mov(0,0,0);p.code[2]=mov(1,1,0);
    p.code[3]=UINT32_C(0x88000000);p.code_defined[3]=p.descriptor_defined[1]=1;
    p.descriptors[1]=15u|(170u<<5);
    for(unsigned u=0;u<CTR_SHADER_UNIFORMS;++u)
        for(unsigned lane=0;lane<4;++lane)p.uniforms[u][lane]=UINT32_C(0x7fc00000);
    p.uniform_defined[95]=4;p.uniforms[95][2]=UINT32_C(0x80000000);
    CHECK(run()==CTR_GX2_SHADER_OK);previous=out;
    CHECK(previous.uniform_count==1 && previous.uniform_order[0]==382 && previous.uniform_used[95]==4);
    revalidation_matches(CTR_GX2_SHADER_OK);
    p.uniforms[95][2]=0;revalidation_matches(CTR_GX2_SHADER_OK);
    p.uniform_defined[95]=0;revalidation_matches(CTR_GX2_SHADER_UNDEFINED);
    /* All 384 lanes fit once, even when first read in descending vector order. */
    init();
    for(unsigned i=0;i<CTR_SHADER_UNIFORMS;++i) {
        unsigned u=CTR_SHADER_UNIFORMS-1u-i;
        p.code[i]=mov(32u+u,16,0);p.code_defined[i]=1;p.uniform_defined[u]=15;
        for(unsigned lane=0;lane<4;++lane)p.uniforms[u][lane]=UINT32_C(0x3f800000);
    }
    p.code[96]=mov(0,0,0);p.code[97]=mov(1,1,0);p.code[98]=UINT32_C(0x88000000);
    p.code_defined[96]=p.code_defined[97]=p.code_defined[98]=1;
    CHECK(run()==CTR_GX2_SHADER_OK);previous=out;
    CHECK(previous.uniform_count==CTR_SHADER_UNIFORMS*4u);
    for(unsigned i=0;i<previous.uniform_count;++i)
        CHECK(previous.uniform_order[i]==(95u-i/4u)*4u+i%4u);
    revalidation_matches(CTR_GX2_SHADER_OK);
    p.uniforms[0][3]=1;revalidation_matches(CTR_GX2_SHADER_NUMERIC);
    init();CHECK(run()==CTR_GX2_SHADER_OK && out.uniform_count==0);
    for(unsigned i=0;i<CTR_SHADER_UNIFORMS*4u;++i)CHECK(out.uniform_order[i]==0);
    previous=out;revalidation_matches(CTR_GX2_SHADER_OK);
}
static void bounds_and_failures(void) {
    init();p.entry=512;failure(CTR_GX2_SHADER_RANGE);
    init();p.code_defined[1]=0;failure(CTR_GX2_SHADER_UNDEFINED);
    init();p.descriptor_defined[0]=0;failure(CTR_GX2_SHADER_UNDEFINED);
    init();p.descriptors[0]|=UINT32_C(0x80000000);failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();p.code[0]|=1u<<19;failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();p.code[2]|=1;failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();p.code[0]=UINT32_C(0x04000000);failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();p.code[0]=UINT32_C(0x84000001);failure(CTR_GX2_SHADER_UNSUPPORTED);
    init();p.entry=509;p.code[509]=p.code[0];p.code[510]=p.code[1];p.code[511]=p.code[2];
    p.code_defined[509]=p.code_defined[510]=p.code_defined[511]=1;
    CHECK(run()==CTR_GX2_SHADER_OK && out.pica_instructions==3);
    init();for(unsigned i=0;i<512;++i) { p.code[i]=UINT32_C(0x84000000);p.code_defined[i]=1; }
    failure(CTR_GX2_SHADER_NO_END);
    /* Maximum code length, maximal binary expansion and many clause boundaries. */
    init();p.code[0]=mov(1,1,0);
    for(unsigned i=1;i<511;++i) { p.code[i]=binary(i&1u?0u:8u,0,1,0,1);p.code_defined[i]=1; }
    p.descriptor_defined[1]=1;p.descriptors[1]=UINT32_C(0x36f)|(27u<<14);
    p.code_defined[511]=1;p.code[511]=UINT32_C(0x88000000);
    CHECK(run()==CTR_GX2_SHADER_OK && out.pica_instructions==512 && out.alu_instructions==6136);
    CHECK(out.size<=CTR_GX2_SHADER_MAX_BYTES);
    unsigned covered=0,clauses=0;
    for(unsigned i=8;(get32(out.program+i+4)&UINT32_C(0xfc000000))==UINT32_C(0xa0000000);i+=8) {
        unsigned count=((get32(out.program+i+4)>>18)&127u)+1u;
        CHECK(get32(out.program+i)==64u+covered);covered+=count;++clauses;
    }
    CHECK(covered==out.alu_instructions && clauses==48);
    init();memset(&out,0xa5,sizeof(out));previous=out;
    CHECK(ctr_gx2_shader_compile(NULL,3,maps,2,&out)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_compile(&p,3,NULL,2,&out)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_compile(&p,3,maps,2,NULL)==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_compile(&p,3,maps,8,&out)==CTR_GX2_SHADER_RANGE);
    CHECK(ctr_gx2_shader_compile(&p,3,maps,2,(ctr_gx2_shader *)((uint8_t *)&out+1))==CTR_GX2_SHADER_ARGUMENT);
    CHECK(ctr_gx2_shader_compile((const ctr_shader_program *)(uintptr_t)(UINTPTR_MAX-3u),3,maps,2,&out)==CTR_GX2_SHADER_OVERFLOW);
    CHECK(ctr_gx2_shader_compile(&p,3,(const uint32_t *)(uintptr_t)(UINTPTR_MAX-3u),2,&out)==CTR_GX2_SHADER_OVERFLOW);
    CHECK(ctr_gx2_shader_compile(&p,3,maps,2,(ctr_gx2_shader *)(uintptr_t)(UINTPTR_MAX-3u))==CTR_GX2_SHADER_OVERFLOW);
    CHECK(!memcmp(&out,&previous,sizeof(out)));
    ctr_gx2_shader *storage=malloc(sizeof(*storage));CHECK(storage!=NULL);
    CHECK(ctr_gx2_shader_compile((const ctr_shader_program *)storage,3,maps,2,storage)==CTR_GX2_SHADER_ALIAS);
    CHECK(ctr_gx2_shader_compile(&p,3,(uint32_t *)storage,2,storage)==CTR_GX2_SHADER_ALIAS);
    free(storage);
    CHECK(!strcmp(ctr_gx2_shader_result_name(CTR_GX2_SHADER_OK),"ok"));
    CHECK(!strcmp(ctr_gx2_shader_result_name((ctr_gx2_shader_result)999),"unknown"));
}
int main(void) {
    original_shader();inputs_uniforms_and_dependencies();swizzles_and_masks();semantic_maps();
    texture_coordinate_exports();uniform_revalidation();bounds_and_failures();
    printf("gx2 shader PASS: %u assertions\n",assertions);return 0;
}
