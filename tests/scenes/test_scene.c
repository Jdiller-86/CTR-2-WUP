#include "ctr_scene_fixture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static uint32_t bits(float f) { uint32_t value;memcpy(&value,&f,sizeof(value));return value; }
static float value(uint32_t b) { float f;memcpy(&f,&b,sizeof(f));return f; }
static void put32(uint8_t *p,uint32_t b) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(b>>(i*8u)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t mov(unsigned source,unsigned dest,unsigned descriptor) {
    return UINT32_C(0x4c000000)|(source<<12)|(dest<<21)|descriptor;
}
static uint32_t binary(unsigned op,unsigned a,unsigned b,unsigned dest,unsigned descriptor) {
    return (op<<26)|(a<<12)|(b<<7)|(dest<<21)|descriptor;
}
static void shader_arithmetic(void) {
    ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out,previous;
    p.code_defined[0]=p.code_defined[1]=p.descriptor_defined[0]=1;
    p.code[1]=UINT32_C(0x88000000);input.enabled=3;
    for(unsigned lane=0;lane<4;++lane) {
        input.input[0][lane]=(float)(1u<<lane);
        input.input[1][lane]=(float)(16u<<lane);
    }
    /* Each operand's complete swizzle/negation/mask space, while the other
     * operand has a different fixed swizzle. Expected values are small integers. */
    for(unsigned op=0;op<=8;op+=8)for(unsigned operand=0;operand<2;++operand)
        for(unsigned selectors=0;selectors<256;++selectors)for(unsigned signs=0;signs<4;++signs)
            for(unsigned mask=0;mask<16;++mask) {
                unsigned s1=operand?27u:selectors,s2=operand?selectors:228u;
                p.descriptors[0]=mask|((signs&1u)<<4)|(s1<<5)|((signs>>1)<<13)|(s2<<14);
                p.code[0]=binary(op,0,1,7,0);
                CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && out.instructions==2);
                CHECK(out.written[7]==((mask&8u)>>3|(mask&4u)>>1|(mask&2u)<<1|(mask&1u)<<3));
                for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
                    float a=input.input[0][(s1>>(6u-lane*2u))&3u]*(signs&1u?-1.0f:1.0f);
                    float b=input.input[1][(s2>>(6u-lane*2u))&3u]*(signs&2u?-1.0f:1.0f);
                    CHECK(out.values[7][lane]==(op?a*b:a+b));
                }
            }
    /* Both sources can alias the destination temporary: all reads precede writes. */
    p.code[0]=mov(0,16,0);p.code[1]=binary(0,16,16,16,1);p.code[2]=mov(16,0,0);
    p.code[3]=UINT32_C(0x88000000);p.code_defined[2]=p.code_defined[3]=p.descriptor_defined[1]=1;
    p.descriptors[0]=UINT32_C(0x36f);p.descriptors[1]=UINT32_C(0x36f)|(228u<<14);
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);
    for(unsigned lane=0;lane<4;++lane)CHECK(out.values[0][lane]==input.input[0][lane]+input.input[0][3u-lane]);
    /* Every register of both encodings, with initialized temporary inputs. */
    input.enabled=65535u;
    for(unsigned reg=0;reg<16;++reg) {
        p.code[reg]=mov(reg,16u+reg,0);p.code_defined[reg]=1;
        for(unsigned lane=0;lane<4;++lane)input.input[reg][lane]=(float)((reg+1u)*(lane+1u));
    }
    for(unsigned reg=0;reg<96;++reg) {
        p.uniform_defined[reg]=15;
        for(unsigned lane=0;lane<4;++lane)p.uniforms[reg][lane]=bits((float)((reg+1u)*(lane+1u)));
    }
    p.code_defined[16]=p.code_defined[17]=1;p.code[17]=UINT32_C(0x88000000);
    p.descriptors[1]=UINT32_C(0x36f)|(27u<<14);
    for(unsigned op=0;op<=8;op+=8)for(unsigned a=0;a<128;++a)for(unsigned b=0;b<32;++b) {
        p.code[16]=binary(op,a,b,15,1);
        CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);
        for(unsigned lane=0;lane<4;++lane) {
            float av=a<32u?input.input[a&15u][lane]:value(p.uniforms[a-32u][lane]);
            float bv=input.input[b&15u][lane];
            CHECK(out.values[15][lane]==(op?av*bv:av+bv));
        }
    }
    /* Late failure must discard earlier output. Undefined/numeric errors on
     * source 2 are observed only in the lanes selected by the destination. */
    memset(&out,0xa5,sizeof(out));previous=out;p.code[0]=mov(0,0,0);
    p.code[1]=binary(8,0,31,1,1);p.code[2]=UINT32_C(0x88000000);
#define FAIL(expected) do { CHECK(ctr_shader_execute(&p,&input,&out)==expected);CHECK(!memcmp(&out,&previous,sizeof(out))); } while(0)
    FAIL(CTR_SHADER_UNDEFINED);
    p.code[1]=binary(0,0,1,1,1);input.enabled&=~2u;FAIL(CTR_SHADER_UNDEFINED);input.enabled|=2u;
    input.input[1][3]=value(UINT32_C(0x7fc00000));FAIL(CTR_SHADER_NUMERIC);
    p.descriptors[1]=UINT32_C(0x36e)|(27u<<14);
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);out=previous;
    p.code[1]|=1u<<19;FAIL(CTR_SHADER_UNSUPPORTED);p.code[1]&=~(1u<<19);
    p.descriptors[1]=0;input.enabled=0;p.code[0]=UINT32_C(0x84000000);
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && !out.written[1]);
#undef FAIL
}
static void arithmetic_boundaries(void) {
    static const struct { unsigned op;uint32_t a,b,result;bool ok; } cases[]={
        {0,0x3f800000,0x37800000,0x3f800080,true}, /* 1 + 2^-16 */
        {0,0x3f800000,0x37000000,0,false},         /* needs an 18th bit */
        {0,0x3f800000,0xb7000000,0x3f7fff80,true}, /* exact cancellation edge */
        {0,0x3f800000,0xb6800000,0,false},
        {0,0x3f800000,0x20800000,0,false},         /* tiny term must not vanish */
        {0,0x3f800000,0xbf800000,0,true},
        {0,0x20800080,0xa0800000,0,false},        /* nonzero subnormal result */
        {0,0x5f7fff80,0x5f7fff80,0,false},
        {0,0x80000000,0x80000000,0x80000000,true},
        {0,0x80000000,0,0,true},
        {0,0,0xbf800000,0xbf800000,true},
        {8,0x3f800080,0x3f800080,0,false},
        {8,0x20800000,0x3f000000,0,false},
        {8,0x5f7fff80,0x40000000,0,false},
        {8,0x20800000,0x5f000000,0x40000000,true},
        {8,0x80000000,0x3f800000,0x80000000,true},
        {8,0x80000000,0xbf800000,0,true},
        {8,0,0xbf800000,0x80000000,true},
        {8,0,0x7f800000,0,false},
        {8,0,1,0,false},
        {0,0x3f800001,0,0,false}
    };
    ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out,previous;
    p.code[1]=UINT32_C(0x88000000);p.code_defined[0]=p.code_defined[1]=1;
    p.descriptor_defined[0]=1;p.descriptors[0]=8;input.enabled=3;
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        p.code[0]=binary(cases[i].op,0,1,0,0);
        memcpy(&input.input[0][0],&cases[i].a,4);memcpy(&input.input[1][0],&cases[i].b,4);
        memset(&out,0xa5,sizeof(out));previous=out;
        CHECK(ctr_shader_execute(&p,&input,&out)==(cases[i].ok?CTR_SHADER_OK:CTR_SHADER_NUMERIC));
        if(cases[i].ok)CHECK(bits(out.values[0][0])==cases[i].result && out.written[0]==1);
        else CHECK(!memcmp(&out,&previous,sizeof(out)));
    }
}
static void floats24(void) {
    const unsigned exponents[]={1,63,126};
    /* Independent mathematical value, not another bit-field conversion. */
    for(unsigned e=0;e<3;++e)for(unsigned sign=0;sign<2;++sign)for(unsigned fraction=0;fraction<65536;++fraction) {
        uint32_t raw=(sign<<23)|(exponents[e]<<16)|fraction,b=0,roundtrip=0;
        CHECK(ctr_shader_f24_decode(raw,&b));
        double expected=ldexp(1.0+(double)fraction/65536.0,(int)exponents[e]-63);
        if(sign)expected=-expected;
        CHECK(value(b)==(float)expected);
        CHECK(ctr_shader_f24_exact(b,&roundtrip) && roundtrip==raw);
    }
    for(unsigned exponent=1;exponent<127;++exponent)for(unsigned sign=0;sign<2;++sign) {
        uint32_t raw=(sign<<23)|(exponent<<16)|12345u,b;
        CHECK(ctr_shader_f24_decode(raw,&b));
        CHECK(value(b)==(float)((sign?-1.0:1.0)*ldexp(1.0+12345.0/65536.0,(int)exponent-63)));
    }
    uint32_t out=UINT32_C(0x12345678);
    CHECK(ctr_shader_f24_decode(0,&out) && out==0);
    CHECK(ctr_shader_f24_decode(UINT32_C(0x800000),&out) && out==UINT32_C(0x80000000));
    const uint32_t rejected24[]={1,UINT32_C(0x800001),UINT32_C(0x7f0000),UINT32_C(0x7f0001),UINT32_C(0xff0000),UINT32_C(0x1000000)};
    for(unsigned i=0;i<sizeof(rejected24)/sizeof(rejected24[0]);++i) {
        out=123;CHECK(!ctr_shader_f24_decode(rejected24[i],&out) && out==123);
    }
    const uint32_t rejected32[]={1,UINT32_C(0x20000000),UINT32_C(0x5f800000),UINT32_C(0x3f800001),UINT32_C(0x7f800000),UINT32_C(0x7fc00000)};
    for(unsigned i=0;i<sizeof(rejected32)/sizeof(rejected32[0]);++i) {
        out=123;CHECK(!ctr_shader_f24_exact(rejected32[i],&out) && out==123);
    }
    CHECK(!ctr_shader_f24_decode(0,NULL));CHECK(!ctr_shader_f24_exact(0,NULL));
}
static void shader_patterns(void) {
    ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out;
    input.enabled=1;
    for(unsigned lane=0;lane<4;++lane)input.input[0][lane]=(float)(1u<<lane);
    p.code[0]=mov(0,7,0);p.code[1]=UINT32_C(0x88000000);
    p.code_defined[0]=p.code_defined[1]=p.descriptor_defined[0]=1;
    for(unsigned selector=0;selector<256;++selector)for(unsigned negate=0;negate<2;++negate)for(unsigned mask=0;mask<16;++mask) {
        p.descriptors[0]=(selector<<5)|(negate<<4)|mask;
        CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);
        CHECK(out.instructions==2);
        for(unsigned lane=0;lane<4;++lane) {
            bool written=(mask&(8u>>lane))!=0;
            CHECK(((out.written[7]>>lane)&1u)==(unsigned)written);
            if(written) {
                unsigned source=(selector>>(6u-lane*2u))&3u;
                CHECK(out.values[7][lane]==(negate?-1.0f:1.0f)*input.input[0][source]);
            }
        }
    }
    /* Every input/temporary/uniform source register, including c95. */
    p.descriptors[0]=UINT32_C(0x36f);input.enabled=65535u;
    for(unsigned reg=0;reg<16;++reg)for(unsigned lane=0;lane<4;++lane)input.input[reg][lane]=(float)(reg*4u+lane+1u);
    for(unsigned reg=0;reg<96;++reg) {
        p.uniform_defined[reg]=15;
        for(unsigned lane=0;lane<4;++lane)p.uniforms[reg][lane]=bits((float)(reg*4u+lane+101u));
    }
    for(unsigned source=0;source<128;++source) {
        p.code[0]=mov(0,source<32u && source>=16u?source:16u,0);
        p.code[1]=mov(source,0,0);p.code[2]=UINT32_C(0x88000000);p.code_defined[2]=1;
        CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);
        for(unsigned lane=0;lane<4;++lane) {
            float expected=source<16u?input.input[source][lane]:source<32u?input.input[0][lane]:value(p.uniforms[source-32u][lane]);
            CHECK(out.values[0][lane]==expected);
        }
    }
    /* In-place reverse swizzle must use the old vector for every lane. */
    p.code[0]=mov(0,16,0);p.code[1]=mov(16,16,1);p.code[2]=mov(16,3,0);
    p.code[3]=UINT32_C(0x84000000);p.code[4]=UINT32_C(0x88000000);
    p.code_defined[3]=p.code_defined[4]=p.descriptor_defined[1]=1;p.descriptors[1]=UINT32_C(0x1c8f);
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && out.instructions==5);
    for(unsigned lane=0;lane<4;++lane)CHECK(out.values[3][lane]==input.input[0][3u-lane]);
    input.input[0][0]=value(UINT32_C(0x80000000));p.descriptors[0]=UINT32_C(0x37f);
    p.code[0]=mov(0,0,0);p.code[1]=UINT32_C(0x88000000);
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && bits(out.values[0][0])==0);
}
static void shader_errors(void) {
    ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out,previous;
    memset(&out,0xa5,sizeof(out));previous=out;
    p.code[0]=mov(0,0,0);p.code[1]=UINT32_C(0x88000000);
    p.code_defined[0]=p.code_defined[1]=p.descriptor_defined[0]=1;p.descriptors[0]=UINT32_C(0x36f);
    input.enabled=1;for(unsigned lane=0;lane<4;++lane)input.input[0][lane]=1.0f;
#define FAIL(expected) do { CHECK(ctr_shader_execute(&p,&input,&out)==expected);CHECK(!memcmp(&out,&previous,sizeof(out))); } while(0)
    input.enabled=0;FAIL(CTR_SHADER_UNDEFINED);input.enabled=1;
    p.code[0]=mov(16,0,0);FAIL(CTR_SHADER_UNDEFINED);
    p.code[0]=mov(32,0,0);p.uniform_defined[0]=1;FAIL(CTR_SHADER_UNDEFINED);
    p.code[0]=mov(0,0,0);p.uniforms[0][1]=UINT32_C(0x7fc00000); /* Unused uniform is not read. */
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK);out=previous;
    input.input[0][3]=value(UINT32_C(0x3f800001));FAIL(CTR_SHADER_NUMERIC);input.input[0][3]=1.0f;
    p.code[0]=UINT32_C(0x04000000);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code[0]=mov(0,0,0)|(1u<<19);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code[0]=mov(0,0,0);p.descriptors[0]|=UINT32_C(0x80000000);FAIL(CTR_SHADER_UNSUPPORTED);
    p.descriptors[0]=UINT32_C(0x36f);p.descriptor_defined[0]=0;FAIL(CTR_SHADER_UNDEFINED);p.descriptor_defined[0]=1;
    p.code[1]=UINT32_C(0x88000001);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code_defined[1]=0;FAIL(CTR_SHADER_UNDEFINED);
    for(unsigned pc=0;pc<CTR_SHADER_MAX_CODE;++pc) { p.code[pc]=UINT32_C(0x84000000);p.code_defined[pc]=1; }
    FAIL(CTR_SHADER_NO_END);p.entry=CTR_SHADER_MAX_CODE;FAIL(CTR_SHADER_RANGE);
    CHECK(ctr_shader_execute(NULL,&input,&out)==CTR_SHADER_ARGUMENT);
    CHECK(ctr_shader_execute(&p,NULL,&out)==CTR_SHADER_ARGUMENT);
    CHECK(ctr_shader_execute(&p,&input,NULL)==CTR_SHADER_ARGUMENT);
    CHECK(ctr_shader_execute((const ctr_shader_program *)(UINTPTR_MAX-3),&input,&out)==CTR_SHADER_OVERFLOW);
    CHECK(!memcmp(&out,&previous,sizeof(out)));
    union { ctr_shader_program program;ctr_shader_output output;ctr_vertex vertex; } aliased;
    memset(&aliased,0xa5,sizeof(aliased));uint8_t before[sizeof(aliased)];memcpy(before,&aliased,sizeof(aliased));
    CHECK(ctr_shader_execute(&aliased.program,&input,&aliased.output)==CTR_SHADER_ALIAS);
    CHECK(!memcmp(before,&aliased,sizeof(aliased)));
    CHECK(ctr_shader_execute(&p,&aliased.vertex,&aliased.output)==CTR_SHADER_ALIAS);
    CHECK(!memcmp(before,&aliased,sizeof(aliased)));
#undef FAIL
}
static void shader_capacity(void) {
    /* Fixed architectural boundary, independent of the implementation macro. */
    CHECK(CTR_SHADER_MAX_CODE==512u);
    ctr_shader_program p={0};ctr_vertex input={0};ctr_shader_output out,previous;
    input.enabled=1;p.descriptors[127]=UINT32_C(0x36f);p.descriptor_defined[127]=1;
    for(unsigned lane=0;lane<4;++lane)input.input[0][lane]=(float)(1u<<lane);
    for(unsigned pc=0;pc<512u;++pc) { p.code[pc]=UINT32_C(0x84000000);p.code_defined[pc]=1; }
    p.code[511]=UINT32_C(0x88000000);
    /* Every executable entry, including both sides of the old 256-word bound.
     * A high MOV and trailing NOPs must reach the actual final END. */
    for(unsigned entry=0;entry<511u;++entry) {
        p.entry=entry;p.code[entry]=mov(0,15,127);
        CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && out.instructions==512u-entry);
        CHECK(out.written[15]==15u);
        for(unsigned lane=0;lane<4;++lane)CHECK(out.values[15][lane]==input.input[0][lane]);
        p.code[entry]=UINT32_C(0x84000000);
    }
    p.entry=511;
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && out.instructions==1);
    for(unsigned reg=0;reg<16;++reg)CHECK(!out.written[reg]);
    memset(&out,0xa5,sizeof(out));previous=out;
#define FAIL(expected) do { CHECK(ctr_shader_execute(&p,&input,&out)==expected);CHECK(!memcmp(&out,&previous,sizeof(out))); } while(0)
    p.code[511]=mov(0,15,127);FAIL(CTR_SHADER_NO_END);
    p.entry=256;p.code_defined[511]=0;FAIL(CTR_SHADER_UNDEFINED);p.code_defined[511]=1;
    p.code[511]=UINT32_C(0x04000000);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code[511]=mov(0,15,127)|(1u<<19);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code[511]=UINT32_C(0x88000001);FAIL(CTR_SHADER_UNSUPPORTED);
    p.code[511]=UINT32_C(0x88000000);p.code_defined[0]=0;
    CHECK(ctr_shader_execute(&p,&input,&out)==CTR_SHADER_OK && out.instructions==256u);out=previous;
    const unsigned invalid[]={512u,513u,65535u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) { p.entry=invalid[i];FAIL(CTR_SHADER_RANGE); }
    ctr_shader_program before=p;
    CHECK(ctr_shader_execute(&p,&input,(ctr_shader_output *)&p.code[256])==CTR_SHADER_ALIAS);
    CHECK(!memcmp(&p,&before,sizeof(p)));
#undef FAIL
}
static void finish(ctr_scene_fixture *f) {
    while(f->command_bytes && (get32(f->commands+f->command_bytes-4u)&65535u)==CTR_PICA_FINALIZE)f->command_bytes-=8u;
    put32(f->commands+f->command_bytes,CTR_PICA_FINALIZE_VALUE);
    put32(f->commands+f->command_bytes+4,UINT32_C(0xf0000)|CTR_PICA_FINALIZE);f->command_bytes+=8;
    if(f->command_bytes&15u) { memcpy(f->commands+f->command_bytes,f->commands+f->command_bytes-8u,8u);f->command_bytes+=8u; }
}
static void append(ctr_scene_fixture *f,unsigned reg,uint32_t v) {
    while(f->command_bytes && (get32(f->commands+f->command_bytes-4u)&65535u)==CTR_PICA_FINALIZE)f->command_bytes-=8u;
    CHECK(f->command_bytes+24u<=sizeof(f->commands));
    put32(f->commands+f->command_bytes,v);put32(f->commands+f->command_bytes+4u,UINT32_C(0xf0000)|reg);
    f->command_bytes+=8u;finish(f);
}
static size_t find(const ctr_scene_fixture *f,unsigned reg,unsigned instance) {
    for(size_t at=0;at<f->command_bytes;at+=8u)if((get32(f->commands+at+4u)&65535u)==reg) {
        if(!instance)return at;
        --instance;
    }
    CHECK(false);return 0;
}
static void set(ctr_scene_fixture *f,unsigned reg,uint32_t v) { put32(f->commands+find(f,reg,0),v); }
static ctr_scene_result execute(ctr_scene_fixture *f,ctr_scene_state *state,uint8_t *pixels,ctr_scene_report *report) {
    ctr_scene_resources r={UINT32_C(0x18000000),f->resources,f->resource_bytes,NULL,0};
    ctr_scene_surface s={pixels,CTR_SCENE_FIXTURE_BYTES,64,64,256};
    return ctr_scene_execute(state,f->commands,f->command_bytes,&r,&s,report);
}
static void golden(const uint8_t *pixels,bool one_color,bool green) {
    unsigned reds=0,greens=0;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint8_t expected[]={16,32,48,255};
        if(x>=8 && x<56 && y>=8 && y<56) {
            bool red=one_color?!green:x+y<63u;
            expected[0]=red?255u:0u;expected[1]=red?0u:255u;expected[2]=0;
            if(red)++reds;else ++greens;
        }
        for(unsigned lane=0;lane<4;++lane)CHECK(pixels[(y*64u+x)*4u+lane]==expected[lane]);
    }
    if(!one_color)CHECK(reds==1128u && greens==1176u);
}
static void scenes(void) {
    ctr_scene_fixture f;ctr_scene_state state;ctr_scene_report report;uint8_t pixels[CTR_SCENE_FIXTURE_BYTES];
    CHECK(ctr_scene_selftest()==0);
    for(unsigned format=0;format<2;++format) {
        ctr_scene_fixture_make(&f,format!=0);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);
        CHECK(report.draws==2 && report.triangles==2 && report.fragments==2304 && report.shader_instructions==18);
        CHECK(state.lists==1 && state.draws==2 && state.triangles==2 && state.fragments==2304 && state.shader_instructions==18);
        golden(pixels,false,false);
    }
    /* The output mask compacts o0/o15 into two semantic mappings. */
    /* Nonindexed draws use vertex_offset; the second reverses winding while
     * culling is disabled and still fills the complementary half-square. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    set(&f,0x22a,0);
    size_t draws[2]={find(&f,0x22f,0),find(&f,0x22f,1)};
    for(unsigned i=0;i<2;++i)put32(f.commands+draws[i]+4u,UINT32_C(0xf0000)|0x22eu);
    size_t second=find(&f,0x22e,1);
    memmove(f.commands+second+8u,f.commands+second,f.command_bytes-second);f.command_bytes+=8u;
    put32(f.commands+second,1);put32(f.commands+second+4u,UINT32_C(0xf0000)|0x22au);finish(&f);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,false,false);

    /* The output mask compacts o0/o15 into two semantic mappings. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    put32(f.commands+find(&f,0x2cc,1),mov(1,15,0));set(&f,0x2bd,UINT32_C(0x8001));
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,false,false);
    /* A third unmapped output has no initialized/claimed lanes. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    set(&f,0x04f,3);set(&f,0x2bd,7);append(&f,0x052,UINT32_C(0x1f1f1f1f));
    /* Put the mapping before the first draw, instead of after it. */
    size_t at=find(&f,0x052,0),first=find(&f,0x22f,0);uint8_t record[8];memcpy(record,f.commands+at,8);
    memmove(f.commands+first+8,f.commands+first,at-first);memcpy(f.commands+first,record,8);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,false,false);
    /* Culling and include/exclude scissor checks are exercised through retained state. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));set(&f,0x040,1);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK && report.fragments==0);
    const uint8_t background[]={16,32,48,255};
    for(unsigned i=0;i<sizeof(pixels);++i)CHECK(pixels[i]==background[i&3u]);
    for(unsigned mode=1;mode<=3;mode+=2) {
        ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));set(&f,0x065,mode);
        /* Scissor defaults to the single pixel (0,0), outside the triangle square. */
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);
        CHECK(report.fragments==(mode==1?2304u:0u));
    }
    /* Uploaded float32 uniform words arrive W,Z,Y,X and feed real c0 MOV. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    put32(f.commands+find(&f,0x2cc,1),mov(32,1,0));
    size_t prefix=find(&f,0x232,0);uint8_t tail[1024];size_t tail_size=f.command_bytes-prefix;
    memcpy(tail,f.commands+prefix,tail_size);f.command_bytes=prefix;
    append(&f,0x2c0,UINT32_C(0x80000000));append(&f,0x2c1,bits(1.0f));append(&f,0x2c1,0);
    append(&f,0x2c1,bits(1.0f));append(&f,0x2c1,0);
    while((get32(f.commands+f.command_bytes-4)&65535u)==CTR_PICA_FINALIZE)f.command_bytes-=8;
    memcpy(f.commands+f.command_bytes,tail,tail_size);f.command_bytes+=tail_size;finish(&f);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,true,true);
    /* The same c0 value in packed float24 arrives as W/Z/Y/X across 3 words. */
    set(&f,0x2c0,0);
    size_t uniform=find(&f,0x2c1,0);
    put32(f.commands+uniform,UINT32_C(0x3f000000));
    put32(f.commands+uniform+8u,UINT32_C(0x00003f00));put32(f.commands+uniform+16u,0);
    memmove(f.commands+uniform+24u,f.commands+uniform+32u,f.command_bytes-uniform-32u);
    f.command_bytes-=8u;finish(&f);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,true,true);
    /* Resolved loader alignment/padding with two array attributes. Color bytes
     * are intentionally 0/1: integer attributes are never normalized. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    uint8_t positions[64];memcpy(positions,f.resources,sizeof(positions));memset(f.resources,0,sizeof(f.resources));
    for(unsigned vertex=0;vertex<4;++vertex) {
        memcpy(f.resources+vertex*24u,positions+vertex*16u,16u);
        f.resources[vertex*24u+16u]=1;f.resources[vertex*24u+19u]=1;
        memset(f.resources+vertex*24u+20u,0xa5,4u);
    }
    const uint8_t order[]={0,1,2,1,3,2};memcpy(f.resources+96u,order,sizeof(order));f.resource_bytes=102u;
    set(&f,0x201,UINT32_C(0xdf));set(&f,0x202,UINT32_C(0x10000000));
    set(&f,0x204,UINT32_C(0xc10));set(&f,0x205,UINT32_C(0x30180000));
    put32(f.commands+find(&f,0x227,0),96u);put32(f.commands+find(&f,0x227,1),99u);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);golden(pixels,true,false);
}
static void arithmetic_scenes(void) {
    ctr_scene_fixture f;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES];
    CHECK(ctr_scene_arithmetic_selftest()==0);
    for(unsigned shorts=0;shorts<2;++shorts) {
        ctr_scene_fixture_background(pixels,sizeof(pixels));
        CHECK(ctr_scene_fixture_render_arithmetic(shorts!=0,pixels,sizeof(pixels),&report)==CTR_SCENE_OK && report.shader_instructions==30);
        golden(pixels,false,false);
        ctr_scene_fixture_make_arithmetic(&f,shorts!=0);ctr_scene_init(&state);
        ctr_scene_fixture_background(pixels,sizeof(pixels));
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK && report.shader_instructions==30);
        golden(pixels,false,false);
    }
    previous=state;memcpy(before,pixels,sizeof(before));
    for(unsigned kind=0;kind<4;++kind) {
        ctr_scene_fixture_make_arithmetic(&f,false);f.command_bytes=find(&f,0x22f,1);
        if(kind==2) {
            append(&f,0x2c0,UINT32_C(0x80000001));append(&f,0x2c1,0);append(&f,0x2c1,0);
            append(&f,0x2c1,UINT32_C(0xbe800000));append(&f,0x2c1,UINT32_C(0x20800000));
        } else {
            append(&f,0x2cb,2);
            append(&f,0x2cc,kind==0?UINT32_C(0x04000000):binary(0,33,kind==1?31u:17u,0,1)|(kind==3?1u<<19:0));
        }
        append(&f,0x22f,1);
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_SHADER && report.draws==1);
        CHECK(report.shader_result==(kind==2?CTR_SHADER_NUMERIC:kind==1?CTR_SHADER_UNDEFINED:CTR_SHADER_UNSUPPORTED));
        CHECK(!memcmp(&previous,&state,sizeof(state)) && !memcmp(before,pixels,sizeof(before)));
    }
}
static void scene_capacity(void) {
    ctr_scene_fixture f;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],old_pixels[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
    memcpy(old_pixels,pixels,sizeof(pixels));
    /* Upload every slot through all eight ports. Alternate consecutive-register
     * batches and repeated-port batches, retaining storage between lists. */
    for(unsigned base=0;base<512u;base+=8u) {
        f.command_bytes=0;append(&f,0x2cb,base);
        while((get32(f.commands+f.command_bytes-4u)&65535u)==CTR_PICA_FINALIZE)f.command_bytes-=8u;
        uint32_t header=UINT32_C(0x007f02cc)|((base&8u)?UINT32_C(0x80000000):0);
        put32(f.commands+f.command_bytes,mov(base&15u,0,0));
        put32(f.commands+f.command_bytes+4u,header);
        for(unsigned k=1;k<8;++k)put32(f.commands+f.command_bytes+4u+k*4u,mov((base+k)&15u,0,0));
        f.command_bytes+=40u;finish(&f);
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);
        CHECK(state.registers[0x2cb]==base+8u && !report.draws);
        CHECK(!memcmp(pixels,old_pixels,sizeof(pixels)));
    }
    for(unsigned pc=0;pc<512u;++pc)CHECK(state.shader.code_defined[pc] && state.shader.code[pc]==mov(pc&15u,0,0));
    /* Cursor 512 is a retained exhausted cursor, never an address or wrap. */
    f.command_bytes=0;append(&f,0x2cb,512);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);previous=state;
    append(&f,0x2d3,UINT32_C(0x88000000));
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_LIMIT && report.reg==0x2d3);
    CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,old_pixels,sizeof(pixels)));
    const unsigned invalid[]={513u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        f.command_bytes=0;append(&f,0x2cb,invalid[i]);
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_LIMIT);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,old_pixels,sizeof(pixels)));
    }
    const unsigned entries[]={254u,255u,256u,509u};
    for(unsigned i=0;i<sizeof(entries)/sizeof(*entries);++i) {
        ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        set(&f,0x2cb,entries[i]);set(&f,0x2ba,entries[i]);
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK && report.shader_instructions==18u);
        CHECK(state.registers[0x2cb]==entries[i]+3u);golden(pixels,false,false);
        previous=state;memcpy(old_pixels,pixels,sizeof(pixels));
        /* Both draws and the last in-range upload succeed in staging. The next
         * word overflows: neither pixels nor either code bank may commit. */
        append(&f,0x2cb,511);append(&f,0x2cc,UINT32_C(0x84000000));append(&f,0x2cc,UINT32_C(0x88000000));
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_LIMIT && report.draws==2u);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,old_pixels,sizeof(pixels)));
    }
}
static void scene_errors(void) {
    ctr_scene_fixture f;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],old_pixels[CTR_SCENE_FIXTURE_BYTES];
#define RESET() do { ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);previous=state;ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(old_pixels,pixels,sizeof(pixels)); } while(0)
#define FAIL(expected) do { CHECK(execute(&f,&state,pixels,&report)==expected);CHECK(!memcmp(&state,&previous,sizeof(state)));CHECK(!memcmp(pixels,old_pixels,sizeof(pixels))); } while(0)
    RESET();append(&f,0x100,0);FAIL(CTR_SCENE_UNSUPPORTED);CHECK(report.draws==2 && report.fragments==2304);
    RESET();f.resources[69]=255;FAIL(CTR_SCENE_VERTEX);CHECK(report.draws==1 && report.vertex_result==CTR_VERTEX_RANGE);
    RESET();put32(f.resources+48,UINT32_C(0x7fc00000));FAIL(CTR_SCENE_VERTEX);CHECK(report.draws==1);
    RESET();set(&f,0x25e,256);FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();set(&f,0x229,2);FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();set(&f,0x228,4);FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x228,258);FAIL(CTR_SCENE_LIMIT);
    RESET();set(&f,0x2bb,0);FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x205,UINT32_C(0x20100000));FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x205,UINT32_C(0x100f0000));FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x204,1);FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x200,UINT32_C(0x03000001));FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();set(&f,0x200,UINT32_C(0x04000000));FAIL(CTR_SCENE_LAYOUT);
    RESET();set(&f,0x2bd,1);FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();set(&f,0x051,UINT32_C(0x0b0b0908));FAIL(CTR_SCENE_SHADER);
    RESET();set(&f,0x051,UINT32_C(0x0b0a0988));FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();set(&f,0x041,UINT32_C(0x7f0000));FAIL(CTR_SCENE_NUMERIC);
    RESET();put32(f.resources,UINT32_C(0x3f800001));FAIL(CTR_SCENE_SHADER);CHECK(report.shader_result==CTR_SHADER_NUMERIC);
    RESET();put32(f.resources+12,bits(2.0f));FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();put32(f.commands+find(&f,0x2cc,1),UINT32_C(0x04000000));FAIL(CTR_SCENE_SHADER);
    RESET();set(&f,0x2ba,512);FAIL(CTR_SCENE_SHADER);CHECK(report.shader_result==CTR_SHADER_RANGE);
    RESET();set(&f,0x2cb,CTR_SHADER_MAX_CODE);FAIL(CTR_SCENE_LIMIT);
    RESET();set(&f,0x2d5,CTR_SHADER_MAX_DESCRIPTORS);FAIL(CTR_SCENE_LIMIT);
    RESET();f.commands[find(&f,0x2cc,0)+6]=0;FAIL(CTR_SCENE_UNSUPPORTED);
    RESET();f.commands[f.command_bytes-8]^=1;FAIL(CTR_SCENE_WIRE);
    RESET();state.lists=UINT64_MAX;previous=state;FAIL(CTR_SCENE_OVERFLOW);
    RESET();state.fragments=UINT64_MAX-2303u;previous=state;FAIL(CTR_SCENE_OVERFLOW);
    RESET();state.fixed_words=3;previous=state;FAIL(CTR_SCENE_LAYOUT);
    RESET();state.uniform_words=3;previous=state;FAIL(CTR_SCENE_LAYOUT);
    /* A nonfinite unused fourth vertex is not visited by the first draw. */
    RESET();put32(f.resources+48,UINT32_C(0x7fc00000));f.command_bytes=find(&f,0x232,1);finish(&f);
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK && report.draws==1 && report.fragments==1128);
    /* Report aliases never damage either input or the render target. */
    RESET();ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,f.resource_bytes,NULL,0};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,(ctr_scene_report *)&state)==CTR_SCENE_ALIAS);
    CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,old_pixels,sizeof(pixels)));
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,(ctr_scene_report *)pixels)==CTR_SCENE_ALIAS);
    CHECK(!memcmp(pixels,old_pixels,sizeof(pixels)));
    surface.bytes=f.commands;surface.size=sizeof(f.commands);surface.width=8;surface.height=8;surface.stride=32;
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_ALIAS);
    surface=(ctr_scene_surface){pixels,sizeof(pixels),64,64,256};resources.bytes=pixels;resources.size=sizeof(pixels);
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_ALIAS);
    resources.bytes=f.resources;resources.size=f.resource_bytes;surface.stride=255;
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_LAYOUT);
    surface.stride=256;surface.height=513;
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_LAYOUT);
    surface.height=64;uint8_t *oversized=malloc(CTR_VERTEX_MAX_SOURCE_BYTES+1u);CHECK(oversized);
    resources.bytes=oversized;resources.size=CTR_VERTEX_MAX_SOURCE_BYTES+1u;
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_LIMIT);
    free(oversized);resources.bytes=f.resources;resources.size=f.resource_bytes;
    CHECK(ctr_scene_execute((ctr_scene_state *)(UINTPTR_MAX-3),f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_OVERFLOW);
    CHECK(!memcmp(pixels,old_pixels,sizeof(pixels)));
    CHECK(!strcmp(ctr_scene_result_name(CTR_SCENE_SHADER),"shader-execution-or-output"));
#undef RESET
#undef FAIL
}
typedef struct {
    const uint8_t *expected;
    size_t size,corrupt;
    unsigned begins,draws,ends,aborts;
} padded_backend;
static bool padded_begin(void *context,const ctr_scene_surface *initial) {
    padded_backend *p=context;++p->begins;
    CHECK(initial->width==64 && initial->height==64 && initial->stride==260 && initial->size==p->size);
    return true;
}
static bool padded_draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,const ctr_scene_texture *texture) {
    CHECK(!texture);
    padded_backend *p=context;++p->draws;CHECK(count==3 && vertices[0].enabled==3);
    CHECK(state->registers[0x11d]==UINT32_C(0x03002000));return true;
}
static bool padded_end(void *context,const ctr_scene_surface *readback) {
    padded_backend *p=context;++p->ends;CHECK(readback->size==p->size && readback->stride==260);
    memcpy(readback->bytes,p->expected,p->size);
    if(p->corrupt)((uint8_t *)readback->bytes)[p->corrupt]^=1u;
    return true;
}
static void padded_abort(void *context) { padded_backend *p=context;++p->aborts; }
static void target_state(ctr_scene_state *state) {
    ctr_scene_init(state);
    const unsigned disabled[]={0x080,0x08f,0x100,0x104,0x105,0x106,0x112,0x114,0x115,0x117,0x11b};
    for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i)state->register_defined[disabled[i]]=15;
    const unsigned regs[]={0x102,0x107,0x113,0x11d,0x11e};
    const uint32_t values[]={3,0xf00,15,UINT32_C(0x03002000),UINT32_C(0x0103f040)};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i) {
        state->registers[regs[i]]=values[i];state->register_defined[regs[i]]=15;
    }
    const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i) {
        state->registers[tev[i]]=i?0xf000f:0;
        state->register_defined[tev[i]]=state->register_defined[tev[i]+1u]=
            state->register_defined[tev[i]+2u]=state->register_defined[tev[i]+4u]=15;
    }
}
static void backend_full_target(void) {
    enum { SIZE=64u*260u+12u };
    ctr_scene_fixture f;ctr_scene_fixture_make(&f,false);
    ctr_scene_state initial,expected,state;target_state(&initial);expected=initial;
    uint8_t pixels[SIZE],old[SIZE],reference[SIZE];ctr_scene_fixture_background(old,sizeof(old));
    memcpy(reference,old,sizeof(old));
    ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,f.resource_bytes,NULL,0};
    ctr_scene_surface surface={reference,sizeof(reference),64,64,260};ctr_scene_report report;
    CHECK(ctr_scene_execute_target(&expected,f.commands,f.command_bytes,&resources,&surface,UINT32_C(0x18010000),&report)==CTR_SCENE_OK);
    for(unsigned kind=0;kind<3;++kind) {
        state=initial;memcpy(pixels,old,sizeof(pixels));surface.bytes=pixels;
        padded_backend context={reference,sizeof(reference),kind==1u?256u:kind==2u?sizeof(reference)-1u:0u,0,0,0,0};
        ctr_scene_backend backend={&context,padded_begin,padded_draw,padded_end,padded_abort};
        CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)==(kind?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        CHECK(context.begins==1 && context.draws==2 && context.ends==1 && context.aborts==(kind?1u:0u));
        CHECK(!memcmp(pixels,kind?old:reference,sizeof(pixels)));
        CHECK(!memcmp(&state,kind?&initial:&expected,sizeof(state)));
        CHECK(report.backend_stage==(kind?CTR_SCENE_BACKEND_PARITY:CTR_SCENE_BACKEND_NONE));
    }
    /* Backend descriptors are protected against writable output aliases and
     * incomplete callback sets fail before any native invocation. */
    padded_backend context={reference,sizeof(reference),0,0,0,0,0};
    union { ctr_scene_backend backend;ctr_scene_report report; } alias;
    alias.backend=(ctr_scene_backend){&context,padded_begin,padded_draw,padded_end,padded_abort};
    ctr_scene_backend previous=alias.backend;
    CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&alias.backend,&alias.report)==CTR_SCENE_ALIAS);
    CHECK(!memcmp(&alias.backend,&previous,sizeof(previous)));
    alias.backend.draw=NULL;
    CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&alias.backend,&report)==CTR_SCENE_ARGUMENT);
    CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
    CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),(const ctr_scene_backend *)(UINTPTR_MAX-3u),&report)==CTR_SCENE_OVERFLOW);
}
static void insert(ctr_scene_fixture *f,size_t at,unsigned reg,uint32_t v) {
    CHECK(f->command_bytes+8u<=sizeof(f->commands));
    memmove(f->commands+at+8u,f->commands+at,f->command_bytes-at);
    put32(f->commands+at,v);put32(f->commands+at+4u,UINT32_C(0xf0000)|reg);f->command_bytes+=8u;
}
static void textured_fixture(ctr_scene_fixture *f,bool shorts,unsigned mode,unsigned wrap,
                             float u0,float u1,float v0,float v1) {
    ctr_scene_fixture_make(f,shorts);uint8_t positions[64];memcpy(positions,f->resources,sizeof(positions));
    memset(f->resources,0,sizeof(f->resources));
    for(unsigned i=0;i<4;++i) {
        memcpy(f->resources+i*24u,positions+i*16u,16u);
        put32(f->resources+i*24u+16u,bits(i&1u?u1:u0));
        put32(f->resources+i*24u+20u,bits(i&2u?v1:v0));
    }
    const uint8_t order[]={0,1,2,1,3,2};unsigned index_bytes=shorts?2u:1u;
    for(unsigned i=0;i<6;++i)f->resources[96u+i*index_bytes]=order[i];
    f->resource_bytes=96u+6u*index_bytes;
    set(f,0x201,UINT32_C(0x7ff));set(f,0x202,UINT32_C(0x20020000));
    set(f,0x204,UINT32_C(0x20));set(f,0x205,UINT32_C(0x20180000));
    set(f,0x2b9,UINT32_C(0xa0000002));set(f,0x2bd,7);set(f,0x04f,3);
    put32(f->commands+find(f,0x227,0),(shorts?UINT32_C(0x80000000):0u)|96u);
    put32(f->commands+find(f,0x227,1),(shorts?UINT32_C(0x80000000):0u)|(96u+3u*index_bytes));
    put32(f->commands+find(f,0x2cc,2),mov(2,2,0));
    insert(f,find(f,0x2bf,0),0x2cc,UINT32_C(0x88000000));
    size_t at=find(f,0x232,0);
    const unsigned regs[]={0x052,0x064,0x06f,0x080,0x082,0x083,0x084,0x085,0x08e};
    const uint32_t values[]={UINT32_C(0x1f1f0d0c),1,0x103,1,UINT32_C(0x00100008),wrap,0,
        UINT32_C(0x03001000),0};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i,at+=8u)insert(f,at,regs[i],values[i]);
    const unsigned tev[]={0xc0,0xc8,0xd0,0xd8,0xf0,0xf8};
    for(unsigned i=0;i<6;++i) {
        insert(f,at,tev[i],i?UINT32_C(0xf000f):UINT32_C(0x30003));at+=8u;
        insert(f,at,tev[i]+1u,0);at+=8u;
        insert(f,at,tev[i]+2u,!i && mode?UINT32_C(0x10001):0u);at+=8u;
        insert(f,at,tev[i]+4u,0);at+=8u;
    }
    finish(f);
}
static unsigned texture_tile(unsigned x,unsigned y,unsigned width) {
    unsigned small=0;
    for(unsigned bit=0;bit<3;++bit)small|=((x>>bit)&1u)<<(2u*bit)|((y>>bit)&1u)<<(2u*bit+1u);
    return ((y/8u)*(width/8u)+x/8u)*64u+small;
}
static void texture_pattern(uint8_t *raw,unsigned width,unsigned height) {
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        uint8_t rgba[]={(uint8_t)(13u*x+17u*y),(uint8_t)(5u*x+29u*y),
            (uint8_t)(7u*x+3u*y+11u),(uint8_t)(19u*x+23u*y+37u)};
        for(unsigned lane=0;lane<4;++lane)raw[texture_tile(x,y,width)*4u+lane]=rgba[3u-lane];
    }
}
static unsigned golden_wrap(int coordinate,unsigned extent,unsigned mode) {
    if(mode==0u)return coordinate<0?0u:coordinate>=(int)extent?extent-1u:(unsigned)coordinate;
    int period=(int)extent*(mode==3u?2:1);
    while(coordinate<0)coordinate+=period;
    while(coordinate>=period)coordinate-=period;
    return mode==3u && coordinate>=(int)extent?(unsigned)(period-coordinate-1):(unsigned)coordinate;
}
static int golden_floor(int64_t n,int64_t d) { return (int)(n>=0?n/d:-((-n+d-1)/d)); }
static void textured_golden(const uint8_t *pixels,unsigned mode,unsigned wrap,
                            float u0,float u1,float v0,float v1) {
    int bounds[4]={(int)(u0*65536.0f),(int)(u1*65536.0f),(int)(v0*65536.0f),(int)(v1*65536.0f)};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint8_t expected[]={16,32,48,255};
        if(x>=8u && x<56u && y>=8u && y<56u) {
            /* Closed affine square coordinates, independent of triangle edges
             * and barycentric arithmetic used by the renderer. */
            int64_t u=(int64_t)bounds[0]*96+((int)x*2-15)*(int64_t)(bounds[1]-bounds[0]);
            int64_t v=(int64_t)bounds[2]*96+((int)y*2-15)*(int64_t)(bounds[3]-bounds[2]);
            unsigned tx=golden_wrap(golden_floor(u*16,96*65536),16,(wrap>>12)&3u);
            unsigned ty=7u-golden_wrap(golden_floor(v*8,96*65536),8,(wrap>>8)&3u);
            expected[0]=(uint8_t)(13u*tx+17u*ty);expected[1]=(uint8_t)(5u*tx+29u*ty);
            expected[2]=(uint8_t)(7u*tx+3u*ty+11u);expected[3]=(uint8_t)(19u*tx+23u*ty+37u);
            if(mode) { bool red=x+y<63u;expected[red?1:0]=0;expected[2]=0; }
        }
        CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4u));
    }
}
static void textured_scenes(void) {
    ctr_scene_fixture f;ctr_scene_state state;ctr_scene_report report;
    uint8_t raw[16u*8u*4u],pixels[CTR_SCENE_FIXTURE_BYTES];texture_pattern(raw,16,8);
    ctr_scene_resource_span span={UINT32_C(0x18008000),raw,sizeof(raw)};
    const unsigned modes[]={0,2,3};
    for(unsigned shorts=0;shorts<2;++shorts)for(unsigned mode=0;mode<2;++mode)
        for(unsigned sw=0;sw<3;++sw)for(unsigned tw=0;tw<3;++tw)for(unsigned range=0;range<3;++range) {
        unsigned wrap=(modes[sw]<<12)|(modes[tw]<<8);
        float u0=range==2?-16.0f:range?-1.0f:0.0f,u1=range==2?16.0f:range?2.0f:1.0f;
        float v0=range==2?16.0f:range?-0.5f:0.0f,v1=range==2?-16.0f:range?1.5f:1.0f;
        textured_fixture(&f,shorts!=0,mode,wrap,u0,u1,v0,v1);
        if(mode && range==1)set(&f,0xc0,UINT32_C(0x300030));
        ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,f.resource_bytes,&span,1};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_OK);
        CHECK(report.draws==2 && report.triangles==2 && report.fragments==2304 && report.shader_instructions==24);
        textured_golden(pixels,mode,wrap,u0,u1,v0,v1);
    }
    /* Exact endpoint UVs exercise clamp, negative repeat and negative mirrored
     * repeat at integer boundaries, rather than only interior sample centers. */
    for(unsigned mode=0;mode<3;++mode)for(int uv=-16;uv<=16;++uv) {
        unsigned wrap=(modes[mode]<<12)|(modes[mode]<<8);
        textured_fixture(&f,false,0,wrap,(float)uv,(float)uv,(float)uv,(float)uv);
        ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,f.resource_bytes,&span,1};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_OK);
        textured_golden(pixels,0,wrap,(float)uv,(float)uv,(float)uv,(float)uv);
    }
    /* The geometry span itself can own the texture; no external map or fixture
     * address is required by the implementation. */
    uint8_t combined[1024]={0};textured_fixture(&f,false,0,0,0,1,0,1);
    memcpy(combined,f.resources,f.resource_bytes);memcpy(combined+512,raw,sizeof(raw));
    set(&f,0x085,UINT32_C(0x18000200)/8u);ctr_scene_init(&state);
    ctr_scene_fixture_background(pixels,sizeof(pixels));
    ctr_scene_resources resources={UINT32_C(0x18000000),combined,sizeof(combined),NULL,0};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
    CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_OK);
    textured_golden(pixels,0,0,0,1,0,1);
    /* Every existing base decoder is connected to sampling, including ETC1
     * signed modifiers and ETC1A4 alpha. All-zero source has an analytic value. */
    uint8_t blank[256]={0};
    static const unsigned alpha[]={0,255,0,255,0,0,255,255,0,0,255,0,255,0};
    static const unsigned bpp[]={32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    for(unsigned format=0;format<14;++format) {
        textured_fixture(&f,false,0,format>=12?32:0,0,1,0,1);set(&f,0x082,UINT32_C(0x00080008));set(&f,0x08e,format);
        ctr_scene_resource_span texture={UINT32_C(0x18008000),blank,8u*8u*bpp[format]/8u};
        resources=(ctr_scene_resources){UINT32_C(0x18000000),f.resources,f.resource_bytes,&texture,1};
        ctr_scene_init(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        CHECK(ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report)==CTR_SCENE_OK);
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            const uint8_t expected[]={(uint8_t)(format>=12?2:0),(uint8_t)(format>=12?2:0),
                (uint8_t)(format>=12?2:0),(uint8_t)alpha[format]};
            CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4u));
        }
    }
}
typedef struct {
    const uint8_t *expected;
    unsigned begins,draws,ends,aborts;
    bool corrupt;
} texture_backend;
static bool texture_begin(void *context,const ctr_scene_surface *initial) {
    texture_backend *p=context;++p->begins;
    CHECK(initial->width==64 && initial->height==64 && initial->stride==256);
    const uint8_t background[]={16,32,48,255};CHECK(!memcmp(initial->bytes,background,4));return true;
}
static bool texture_draw(void *context,const ctr_scene_state *s,const ctr_vertex *vertices,unsigned count,
                         const ctr_scene_texture *texture) {
    texture_backend *p=context;++p->draws;CHECK(count==3 && vertices[0].enabled==7);
    CHECK(s->registers[0x080]==1 && texture && texture->rgba && texture->width==16 && texture->height==8);
    CHECK(texture->address==UINT32_C(0x18008000) && !texture->format && !texture->params && !texture->tev_mode);
    const uint8_t first[]={0,0,11,37},lastrow[]={119,203,32,198};
    CHECK(!memcmp(texture->rgba,first,4) && !memcmp(texture->rgba+7u*16u*4u,lastrow,4));
    CHECK(vertices[0].input[2][0]==(p->draws==1u?0.0f:1.0f));return true;
}
static bool texture_end(void *context,const ctr_scene_surface *readback) {
    texture_backend *p=context;++p->ends;memcpy(readback->bytes,p->expected,readback->size);
    if(p->corrupt)((uint8_t *)readback->bytes)[(16u*64u+16u)*4u]^=1u;
    return true;
}
static void texture_abort(void *context) { texture_backend *p=context;++p->aborts; }
static void textured_rollback(void) {
    ctr_scene_fixture f;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t raw[512],raw_before[512],pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES],reference[CTR_SCENE_FIXTURE_BYTES];
    texture_pattern(raw,16,8);memcpy(raw_before,raw,sizeof(raw));
    ctr_scene_resource_span span={UINT32_C(0x18008000),raw,sizeof(raw)};
    ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,0,&span,1};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
    textured_fixture(&f,false,0,0,0,1,0,1);resources.size=f.resource_bytes;target_state(&state);
    ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));
    CHECK(ctr_scene_execute_target(&state,f.commands,f.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&report)==CTR_SCENE_OK);memcpy(reference,pixels,sizeof(reference));
    /* The payload contains actual decoded source rows and original guest UV
     * attributes; exact full-target parity still guards the native commit. */
    for(unsigned corrupt=0;corrupt<2;++corrupt) {
        textured_fixture(&f,false,0,0,0,1,0,1);resources.size=f.resource_bytes;
        target_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
        texture_backend context={reference,0,0,0,0,corrupt!=0};
        ctr_scene_backend backend={&context,texture_begin,texture_draw,texture_end,texture_abort};
        CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)==(corrupt?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        CHECK(context.begins==1 && context.draws==2 && context.ends==1 && context.aborts==corrupt);
        CHECK(!memcmp(pixels,corrupt?before:reference,sizeof(pixels)));
        if(corrupt)CHECK(!memcmp(&state,&previous,sizeof(state)));
    }
    for(unsigned kind=0;kind<19;++kind) {
        textured_fixture(&f,false,0,0,0,1,0,1);resources.size=f.resource_bytes;
        target_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
        if(kind==0)append(&f,0x083,2); /* Late unsupported magnification filter. */
        if(kind==1)append(&f,0x083,UINT32_C(0x100)); /* Border wrap. */
        if(kind==2)append(&f,0x084,1); /* Unsupported mip/LOD. */
        if(kind==3)append(&f,0xc2,2); /* Unsupported TEV after both draws. */
        if(kind==4)append(&f,0xc1,1);
        if(kind==5)append(&f,0xc4,1);
        if(kind==6)append(&f,0x08e,14);
        if(kind==7)append(&f,0x080,3);
        if(kind==8)set(&f,0x082,UINT32_C(0x000f0008));
        if(kind==9)set(&f,0x085,(span.base_address+(uint32_t)span.size-8u)/8u);
        if(kind==10)put32(f.resources+16u,bits(17.0f));
        if(kind==11)put32(f.resources+16u,bits(1.0f/131072.0f));
        if(kind==12)set(&f,0x052,UINT32_C(0x1f1f1f0c));
        if(kind==13)set(&f,0x064,0);
        if(kind==14)set(&f,0x06f,3);
        if(kind==15) { /* Compressed format must carry its real format flag. */
            set(&f,0x08e,12);set(&f,0x082,UINT32_C(0x00080008));
        }
        if(kind==16)set(&f,0x083,32); /* Noncompressed format cannot carry it. */
        if(kind==17) { /* The first staged draw succeeds before source failure. */
            insert(&f,find(&f,0x22f,1),0x085,(span.base_address+(uint32_t)span.size-8u)/8u);finish(&f);
        }
        if(kind==18) {
            set(&f,0x08e,12);set(&f,0x082,UINT32_C(0x00080008));set(&f,0x083,32);
            memset(raw,0,sizeof(raw));put32(raw+4,UINT32_C(0x702)); /* Invalid ETC differential block. */
        }
        texture_backend context={reference,0,0,0,0,false};
        ctr_scene_backend backend={&context,texture_begin,texture_draw,texture_end,texture_abort};
        CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)!=CTR_SCENE_OK);
        CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        CHECK(!memcmp(pixels,before,sizeof(pixels)) && !memcmp(&state,&previous,sizeof(state)));
        if(kind<8u)CHECK(report.draws==2);
        if(kind==17u)CHECK(report.draws==1);
        memcpy(raw,raw_before,sizeof(raw));
    }
    /* Metadata and source aliases, physical ambiguity, wraparound and aggregate
     * bounds fail before any writes to state, target or borrowed source data. */
    for(unsigned kind=0;kind<12;++kind) {
        textured_fixture(&f,false,0,0,0,1,0,1);target_state(&state);previous=state;
        memcpy(pixels,before,sizeof(pixels));
        ctr_scene_resource_span spans[2]={{UINT32_C(0x18008000),raw,sizeof(raw)},
                                        {UINT32_C(0x18009000),raw,sizeof(raw)}};
        resources=(ctr_scene_resources){UINT32_C(0x18000000),f.resources,f.resource_bytes,spans,1};
        ctr_scene_result expected=CTR_SCENE_ALIAS;
        uint8_t *oversized=NULL;
        if(kind==0)resources.textures=(const ctr_scene_resource_span *)&state;
        if(kind==1)resources.textures=(const ctr_scene_resource_span *)pixels;
        if(kind==2)resources.textures=(const ctr_scene_resource_span *)&report;
        if(kind==3)spans[0].bytes=&state;
        if(kind==4)spans[0].bytes=pixels;
        if(kind==5)spans[0].bytes=&report;
        if(kind==6)spans[0].base_address=resources.base_address;
        if(kind==7) { resources.texture_count=2;spans[1].base_address=spans[0].base_address; }
        if(kind==8) { resources.texture_count=33;expected=CTR_SCENE_LIMIT; }
        if(kind==9) { spans[0].base_address=UINT32_MAX;expected=CTR_SCENE_OVERFLOW; }
        if(kind==10) { spans[0].size=0;expected=CTR_SCENE_LAYOUT; }
        if(kind==11) {
            oversized=malloc(CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES+1u);CHECK(oversized);
            spans[0].bytes=oversized;spans[0].size=CTR_SCENE_MAX_TEXTURE_SOURCE_BYTES+1u;expected=CTR_SCENE_LIMIT;
        }
        ctr_scene_result actual=ctr_scene_execute(&state,f.commands,f.command_bytes,&resources,&surface,&report);
        if(actual!=expected)fprintf(stderr,"texture alias case %u: actual=%s expected=%s\n",kind,
            ctr_scene_result_name(actual),ctr_scene_result_name(expected));
        CHECK(actual==expected);
        CHECK(!memcmp(pixels,before,sizeof(pixels)) && !memcmp(&state,&previous,sizeof(state)));
        CHECK(!memcmp(raw,raw_before,sizeof(raw)));
        free(oversized);
    }
}
int main(void) {
    floats24();shader_patterns();shader_arithmetic();arithmetic_boundaries();shader_errors();shader_capacity();
    scenes();arithmetic_scenes();scene_capacity();scene_errors();backend_full_target();textured_scenes();textured_rollback();
    printf("original shader/scene tests: %u assertions passed\n",assertions);return 0;
}
