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
    RESET();append(&f,0x100,1);FAIL(CTR_SCENE_UNSUPPORTED);CHECK(report.draws==2 && report.fragments==2304);
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
        ctr_scene_backend backend={&context,padded_begin,padded_draw,padded_end,padded_abort,NULL,NULL,NULL};
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
    alias.backend=(ctr_scene_backend){&context,padded_begin,padded_draw,padded_end,padded_abort,NULL,NULL,NULL};
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
    CHECK(texture->address==UINT32_C(0x18008000) && !texture->format &&
        (texture->params==0u || texture->params==6u));
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
    for(unsigned filter=0;filter<2;++filter) {
        textured_fixture(&f,false,0,filter?6u:0u,0,1,0,1);resources.size=f.resource_bytes;target_state(&state);
        ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));
        CHECK(ctr_scene_execute_target(&state,f.commands,f.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&report)==CTR_SCENE_OK);memcpy(reference,pixels,sizeof(reference));
        /* The payload contains actual decoded source rows and original guest UV
         * attributes; exact full-target parity still guards the native commit. */
        for(unsigned corrupt=0;corrupt<2;++corrupt) {
            textured_fixture(&f,false,0,filter?6u:0u,0,1,0,1);resources.size=f.resource_bytes;
            target_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
            texture_backend context={reference,0,0,0,0,corrupt!=0};
            ctr_scene_backend backend={&context,texture_begin,texture_draw,texture_end,texture_abort,NULL,NULL,NULL};
            CHECK(ctr_scene_execute_target_backend(&state,f.commands,f.command_bytes,&resources,&surface,
                  UINT32_C(0x18010000),&backend,&report)==(corrupt?CTR_SCENE_BACKEND:CTR_SCENE_OK));
            CHECK(context.begins==1 && context.draws==2 && context.ends==1 && context.aborts==corrupt);
            CHECK(!memcmp(pixels,corrupt?before:reference,sizeof(pixels)));
            if(corrupt)CHECK(!memcmp(&state,&previous,sizeof(state)));
        }
    }
    for(unsigned kind=0;kind<19;++kind) {
        textured_fixture(&f,false,0,0,0,1,0,1);resources.size=f.resource_bytes;
        target_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
        if(kind==0)append(&f,0x083,2); /* Late unsupported magnification filter. */
        if(kind==1)append(&f,0x083,UINT32_C(0x100)); /* Border wrap. */
        if(kind==2)append(&f,0x084,1); /* Unsupported mip/LOD. */
        if(kind==3)append(&f,0xc2,6); /* Unsupported TEV after both draws. */
        if(kind==4)append(&f,0xc1,6);
        if(kind==5)append(&f,0xc4,3);
        if(kind==6)append(&f,0x08e,14);
        if(kind==7)append(&f,0x080,8); /* Reserved texture configuration bit. */
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
        ctr_scene_backend backend={&context,texture_begin,texture_draw,texture_end,texture_abort,NULL,NULL,NULL};
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
/* Independent Boolean truth tables: bit index = source*2 + destination. */
static uint8_t merger_expected(unsigned op,unsigned source,unsigned destination) {
    static const unsigned truth[]={0,8,4,12,15,3,10,5,7,14,1,6,9,2,13,11};
    unsigned result=0;
    for(unsigned bit=0;bit<8;++bit) {
        unsigned index=(((source>>bit)&1u)<<1)|((destination>>bit)&1u);
        result|=((truth[op]>>index)&1u)<<bit;
    }
    return (uint8_t)result;
}
static void fragment_contract(void) {
    ctr_fragment_state state={.logic_op=3,.write_mask=15},out,previous;
    /* Every source/destination byte pair for every operation, including channel
     * independence and source/destination aliases. No renderer code is an oracle. */
    for(unsigned op=0;op<16;++op)for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b) {
        uint8_t source[4]={(uint8_t)a,(uint8_t)(255u-a),(uint8_t)b,137};
        uint8_t pixel[4]={(uint8_t)b,(uint8_t)a,183,(uint8_t)b},old[4];
        memcpy(old,pixel,4);state.logic_op=(uint8_t)op;
        CHECK(ctr_fragment_apply(&state,source,pixel));
        for(unsigned lane=0;lane<4;++lane)CHECK(pixel[lane]==merger_expected(op,source[lane],old[lane]));
    }
    for(unsigned op=0;op<16;++op)for(unsigned mask=0;mask<16;++mask) {
        uint8_t pixel[4]={19,67,139,211},old[4];memcpy(old,pixel,4);
        state.logic_op=(uint8_t)op;state.write_mask=(uint8_t)mask;
        CHECK(ctr_fragment_apply(&state,pixel,pixel));
        for(unsigned lane=0;lane<4;++lane)
            CHECK(pixel[lane]==(mask&(1u<<lane)?merger_expected(op,old[lane],old[lane]):old[lane]));
    }
    state=(ctr_fragment_state){.logic_op=4,.write_mask=15,.alpha_enabled=true};
    /* Exhaust all 256x256 alpha/reference pairs for every comparison. The
     * expected ordering is independent of comparison dispatch implementation. */
    for(unsigned function=0;function<8;++function)for(unsigned alpha=0;alpha<256;++alpha)
        for(unsigned reference=0;reference<256;++reference) {
            bool relation[]={false,true,alpha==reference,alpha!=reference,alpha<reference,
                alpha<=reference,alpha>reference,alpha>=reference};
            uint8_t source[4]={29,101,173,(uint8_t)alpha},pixel[4]={7,43,149,229},old[4];
            memcpy(old,pixel,4);state.alpha_function=(uint8_t)function;state.alpha_reference=(uint8_t)reference;
            CHECK(ctr_fragment_apply(&state,source,pixel)==relation[function]);
            for(unsigned lane=0;lane<4;++lane)CHECK(pixel[lane]==(relation[function]?255u:old[lane]));
        }
    uint32_t registers[CTR_PICA_REGISTER_COUNT]={0};uint8_t defined[CTR_PICA_REGISTER_COUNT]={0};
    CHECK(ctr_fragment_decode(registers,defined,false,&out));
    CHECK(out.logic_op==3 && out.write_mask==15 && !out.alpha_enabled);
    memset(&out,0xa5,sizeof(out));previous=out;
    CHECK(!ctr_fragment_decode(registers,defined,true,&out));CHECK(!memcmp(&out,&previous,sizeof(out)));
    CHECK(!ctr_fragment_decode(NULL,defined,false,&out));CHECK(!ctr_fragment_decode(registers,NULL,false,&out));
    CHECK(!ctr_fragment_decode(registers,defined,false,NULL));
    const unsigned regs[]={0x100,0x102,0x104,0x107,0x112,0x113};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i)defined[regs[i]]=15;
    registers[0x102]=3;registers[0x107]=0xf00;registers[0x113]=15;
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i)for(unsigned mask=0;mask<15;++mask) {
        defined[regs[i]]=(uint8_t)mask;
        CHECK(!ctr_fragment_decode(registers,defined,false,&out));CHECK(!memcmp(&out,&previous,sizeof(out)));
        defined[regs[i]]=15;
    }
    for(unsigned op=0;op<16;++op)for(unsigned mask=0;mask<16;++mask)
        for(unsigned read=0;read<2;++read)for(unsigned write=0;write<2;++write) {
            registers[0x102]=op;registers[0x107]=mask<<8;
            registers[0x112]=read?15u:0u;registers[0x113]=write?15u:0u;
            bool needs_read=write && mask && op!=0u && op!=3u && op!=4u && op!=5u;
            out=previous;CHECK(ctr_fragment_decode(registers,defined,true,&out)==(!needs_read || read));
            if(needs_read && !read)CHECK(!memcmp(&out,&previous,sizeof(out)));
            else CHECK(out.logic_op==op && out.write_mask==(write?mask:0u));
        }
    for(unsigned reg=0;reg<CTR_PICA_REGISTER_COUNT;++reg) {
        bool recognized=reg==0x101u || reg==0x103u;
        for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i)if(reg==regs[i])recognized=true;
        CHECK(ctr_fragment_register(reg)==recognized);
        if(!recognized)CHECK(!ctr_fragment_register_value(reg,0));
    }
    for(unsigned bit=0;bit<32;++bit) {
        uint32_t word=UINT32_C(1)<<bit;
        CHECK(ctr_fragment_register_value(0x100,word)==(bit==8u));
        CHECK(ctr_fragment_register_value(0x101,word)==(bit<3u || (bit>=8u && bit<11u) || bit>=16u));
        CHECK(ctr_fragment_register_value(0x102,word)==(bit<4u));
        CHECK(ctr_fragment_register_value(0x103,word));
        CHECK(ctr_fragment_register_value(0x104,word)==((word&UINT32_C(0xff71))!=0u));
        CHECK(ctr_fragment_register_value(0x107,word)==((word&UINT32_C(0x1f71))!=0u));
        CHECK(!ctr_fragment_register_value(0x112,word));CHECK(!ctr_fragment_register_value(0x113,word));
    }
    for(unsigned function=0;function<8;++function)for(unsigned reference=0;reference<256;++reference)
        for(unsigned enabled=0;enabled<2;++enabled) {
            registers[0x102]=3;registers[0x104]=enabled|(function<<4)|(reference<<8);
            CHECK(ctr_fragment_decode(registers,defined,true,&out));
            CHECK(out.alpha_enabled==(enabled!=0) && out.alpha_function==function && out.alpha_reference==reference);
            uint8_t source[4]={1,2,3,4},pixel[4]={5,6,7,8};out.alpha_enabled=false;out.write_mask=15;
            CHECK(ctr_fragment_apply(&out,source,pixel) && !memcmp(source,pixel,4));
        }
}
static void fragment_scenes(void) {
    ctr_scene_fixture f;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_fixture_background(before,sizeof(before));
    /* A complete explicit merger also works in the owned, unbound API. Every
     * channel-mask/logic pair preserves pixels outside the analytic triangle. */
    for(unsigned op=0;op<16;++op)for(unsigned mask=0;mask<16;++mask) {
        ctr_scene_fixture_make(&f,false);target_state(&state);
        state.registers[0x102]=op;state.registers[0x107]=mask<<8;state.registers[0x112]=15;
        memcpy(pixels,before,sizeof(pixels));CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK);
        CHECK(report.fragments==2304 && report.draws==2 && report.triangles==2);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)for(unsigned lane=0;lane<4;++lane) {
            size_t offset=(y*64u+x)*4u+lane;uint8_t expected=before[offset];
            if(x>=8 && x<56 && y>=8 && y<56 && (mask&(1u<<lane))) {
                uint8_t source[4]={(uint8_t)(x+y<63u?255u:0u),(uint8_t)(x+y<63u?0u:255u),0,255};
                expected=merger_expected(op,source[lane],expected);
            }
            CHECK(pixels[offset]==expected);
        }
    }
    /* A disabled write gate and an alpha rejection preserve the full target
     * while covered-fragment counters retain their geometric meaning. */
    for(unsigned kind=0;kind<2;++kind) {
        ctr_scene_fixture_make(&f,false);target_state(&state);memcpy(pixels,before,sizeof(pixels));
        if(kind)state.registers[0x104]=1;else state.registers[0x113]=0;
        CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_OK && report.fragments==2304);
        CHECK(!memcmp(pixels,before,sizeof(pixels)));
    }
    const unsigned regs[]={0x100,0x102,0x104,0x107,0x112,0x113};
    const uint32_t invalid[]={1,16,2,1,1,1};
    for(unsigned bound=0;bound<2;++bound)for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i) {
        ctr_scene_fixture_make(&f,false);target_state(&state);previous=state;
        memcpy(pixels,before,sizeof(pixels));append(&f,regs[i],invalid[i]);
        ctr_scene_resources resources={UINT32_C(0x18000000),f.resources,f.resource_bytes,NULL,0};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        ctr_scene_result result=bound?ctr_scene_execute_target(&state,f.commands,f.command_bytes,&resources,&surface,
            UINT32_C(0x18010000),&report):execute(&f,&state,pixels,&report);
        CHECK(result==CTR_SCENE_UNSUPPORTED && report.draws==2 && report.reg==regs[i]);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    }
    /* One explicitly configured register cannot silently inherit defaults for
     * the rest; complete byte-mask state is required at each actual draw. */
    ctr_scene_fixture_make(&f,false);ctr_scene_init(&state);memcpy(pixels,before,sizeof(pixels));
    insert(&f,0,0x102,3);finish(&f);previous=state;
    CHECK(execute(&f,&state,pixels,&report)==CTR_SCENE_UNSUPPORTED && !report.draws);
    CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
}
/* The reference constructs all fifteen scalar weights together and evaluates
 * a binary64 rational quotient. Integer products/sums <=130050 are exact in
 * binary64; division cannot cross an integer boundary except at exact integral
 * quotients, which are representable. No runtime factor/equation helper is used. */
static unsigned blend_weight(unsigned factor,unsigned lane,const uint8_t source[4],
                              const uint8_t old[4],const uint8_t constant[4]) {
    unsigned saturated=lane==3u?255u:(source[3]+old[3]<=255u?source[3]:255u-old[3]);
    const unsigned weights[]={0,255,source[lane],255u-source[lane],old[lane],255u-old[lane],
        source[3],255u-source[3],old[3],255u-old[3],constant[lane],255u-constant[lane],
        constant[3],255u-constant[3],saturated};
    return weights[factor];
}
static uint8_t blend_expected(unsigned equation,unsigned sf,unsigned df,unsigned lane,
                               const uint8_t source[4],const uint8_t old[4],const uint8_t constant[4]) {
    double a=(double)source[lane]*blend_weight(sf,lane,source,old,constant);
    double b=(double)old[lane]*blend_weight(df,lane,source,old,constant);
    double expressions[]={a+b,a-b,b-a,fmin(a,b),fmax(a,b)};
    double result=expressions[equation]/255.0;
    return result<=0.0?0u:result>=255.0?255u:(uint8_t)floor(result);
}
static uint32_t blend_word(unsigned rgb,unsigned alpha,unsigned srgb,unsigned drgb,unsigned sa,unsigned da) {
    return rgb|(alpha<<8)|(srgb<<16)|(drgb<<20)|(sa<<24)|(da<<28);
}
static void blend_contract(void) {
    ctr_fragment_state state={.write_mask=15,.blend_enabled=true,.blend_source={6,1},.blend_destination={7,0}};
    uint8_t source[]={90,195,105,126},pixel[]={16,32,48,255};
    CHECK(ctr_fragment_apply(&state,source,pixel));
    const uint8_t over[]={52,112,76,126};CHECK(!memcmp(pixel,over,4));
    /* One final division: floor(127/255 + 128/255) = 1, while separately
     * truncating the products would incorrectly produce zero. */
    state.blend_source[0]=10;state.blend_destination[0]=12;
    memset(state.blend_constant,127,3);state.blend_constant[3]=128;
    memset(source,1,4);memset(pixel,1,4);CHECK(ctr_fragment_apply(&state,source,pixel));
    for(unsigned lane=0;lane<4;++lane)CHECK(pixel[lane]==1);
    /* Factor-aware MIN/MAX differ from ordinary unweighted color MIN/MAX. */
    memset(source,200,4);memset(pixel,100,4);memset(state.blend_constant,32,3);state.blend_constant[3]=200;
    state.blend_equation[0]=3;CHECK(ctr_fragment_apply(&state,source,pixel));
    for(unsigned lane=0;lane<3;++lane)CHECK(pixel[lane]==25);
    memset(pixel,100,4);state.blend_equation[0]=4;CHECK(ctr_fragment_apply(&state,source,pixel));
    for(unsigned lane=0;lane<3;++lane)CHECK(pixel[lane]==78);
    /* Saturate is min(Sa,255-Da) for RGB and ONE for alpha. */
    state=(ctr_fragment_state){.write_mask=15,.blend_enabled=true,.blend_source={14,14}};
    const uint8_t saturated_source[]={200,100,50,64},saturated_old[]={70,80,90,233},saturated_expected[]={17,8,4,64};
    memcpy(pixel,saturated_old,4);CHECK(ctr_fragment_apply(&state,saturated_source,pixel));CHECK(!memcmp(pixel,saturated_expected,4));
    unsigned vectors=0;
    for(unsigned sf=0;sf<15;++sf)for(unsigned df=0;df<15;++df)
        for(unsigned equation=0;equation<5;++equation)for(unsigned vector=0;vector<16;++vector) {
        uint8_t old[4];
        for(unsigned lane=0;lane<4;++lane) {
            source[lane]=(uint8_t)(vector*43u+lane*67u);
            old[lane]=(uint8_t)(vector*71u+lane*19u+3u);
            state.blend_constant[lane]=(uint8_t)(vector*29u+lane*53u+127u);
        }
        state.blend_equation[0]=(uint8_t)equation;state.blend_equation[1]=(uint8_t)((equation+2u)%5u);
        state.blend_source[0]=(uint8_t)sf;state.blend_source[1]=(uint8_t)((sf+7u)%15u);
        state.blend_destination[0]=(uint8_t)df;state.blend_destination[1]=(uint8_t)((df+11u)%15u);
        state.logic_op=(uint8_t)(vector&15u);memcpy(pixel,old,4);
        CHECK(ctr_fragment_apply(&state,source,pixel));++vectors;
        for(unsigned lane=0;lane<4;++lane) {
            unsigned side=lane==3u;
            CHECK(pixel[lane]==blend_expected(state.blend_equation[side],state.blend_source[side],
                state.blend_destination[side],lane,source,old,state.blend_constant));
        }
    }
    CHECK(vectors==18000u);
    /* Every byte pair for all equations under source-alpha interpolation and
     * source-alpha-saturate. Endpoints and negative/saturating numerators are
     * included; alpha has independent factors and sees the captured old bytes. */
    for(unsigned mode=0;mode<2;++mode)for(unsigned equation=0;equation<5;++equation)
        for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b) {
        uint8_t old[]={(uint8_t)b,(uint8_t)a,(uint8_t)(255u-b),(uint8_t)b};
        source[0]=(uint8_t)a;source[1]=(uint8_t)b;source[2]=173;source[3]=(uint8_t)a;
        state.blend_equation[0]=state.blend_equation[1]=(uint8_t)equation;
        state.blend_source[0]=state.blend_source[1]=(uint8_t)(mode?14u:6u);
        state.blend_destination[0]=state.blend_destination[1]=7;memcpy(pixel,old,4);
        CHECK(ctr_fragment_apply(&state,source,pixel));
        for(unsigned lane=0;lane<4;++lane)CHECK(pixel[lane]==blend_expected(equation,mode?14u:6u,7,lane,source,old,state.blend_constant));
    }
    for(unsigned mask=0;mask<16;++mask) {
        uint8_t storage[]={19,67,139,211,47},old[4],input[4];memcpy(old,storage,4);memcpy(input,storage+1,4);
        state.write_mask=(uint8_t)mask;
        CHECK(ctr_fragment_apply(&state,storage+1,storage));
        for(unsigned lane=0;lane<4;++lane)CHECK(storage[lane]==(mask&(1u<<lane)?
            blend_expected(4,14,7,lane,input,old,state.blend_constant):old[lane]));
    }
    state.alpha_enabled=true;state.alpha_function=0;memcpy(pixel,source,4);
    CHECK(!ctr_fragment_apply(&state,source,pixel) && !memcmp(pixel,source,4));

    uint32_t registers[CTR_PICA_REGISTER_COUNT]={0};uint8_t defined[CTR_PICA_REGISTER_COUNT]={0};
    const unsigned base[]={0x100,0x102,0x104,0x107,0x112,0x113};
    for(unsigned i=0;i<sizeof(base)/sizeof(*base);++i)defined[base[i]]=15;
    registers[0x100]=0x100;registers[0x102]=15;registers[0x107]=0xf00;registers[0x112]=registers[0x113]=15;
    ctr_fragment_state decoded,before;memset(&before,0xa5,sizeof(before));decoded=before;
    CHECK(!ctr_fragment_decode(registers,defined,true,&decoded) && !memcmp(&decoded,&before,sizeof(decoded)));
    defined[0x101]=15;registers[0x101]=blend_word(0,2,6,7,1,0);
    CHECK(ctr_fragment_decode(registers,defined,true,&decoded));
    CHECK(decoded.blend_enabled && decoded.logic_op==15 && decoded.blend_equation[1]==2);
    CHECK(decoded.blend_source[0]==6 && decoded.blend_destination[0]==7 && decoded.blend_source[1]==1 && !decoded.blend_destination[1]);
    const uint8_t zero[4]={0};CHECK(!memcmp(decoded.blend_constant,zero,4));
    for(unsigned shift=16;shift<32;shift+=4u)for(unsigned factor=0;factor<16;++factor)
        CHECK(ctr_fragment_register_value(0x101,factor<<shift)==(factor<15u));
    for(unsigned side=0;side<2;++side)for(unsigned equation=0;equation<8;++equation)
        CHECK(ctr_fragment_register_value(0x101,equation<<(side*8u))==(equation<5u));
    for(unsigned factor=10;factor<14;++factor)for(unsigned slot=0;slot<4;++slot)for(unsigned mask=0;mask<15;++mask) {
        registers[0x101]=factor<<(16u+slot*4u);defined[0x103]=(uint8_t)mask;
        registers[0x107]=0;decoded=before;
        CHECK(!ctr_fragment_decode(registers,defined,true,&decoded) && !memcmp(&decoded,&before,sizeof(decoded)));
    }
    defined[0x103]=15;registers[0x103]=UINT32_C(0x537b8d25);
    const uint8_t constants[]={37,141,123,83};
    for(unsigned sf=0;sf<15;++sf)for(unsigned df=0;df<15;++df)for(unsigned mask=0;mask<16;++mask)
        for(unsigned read=0;read<2;++read) {
        unsigned as=(sf+7u)%15u,ad=(df+11u)%15u;
        registers[0x101]=blend_word(3,4,sf,df,as,ad);registers[0x107]=mask<<8;registers[0x112]=read?15u:0u;
        bool needs=((mask&7u) && (df || ((UINT32_C(0x4330)>>sf)&1u))) ||
                   ((mask&8u) && (ad || ((UINT32_C(0x330)>>as)&1u)));
        decoded=before;CHECK(ctr_fragment_decode(registers,defined,true,&decoded)==(!needs || read));
        if(needs && !read)CHECK(!memcmp(&decoded,&before,sizeof(decoded)));
        else CHECK(decoded.write_mask==mask && !memcmp(decoded.blend_constant,constants,4));
    }
    registers[0x113]=0;registers[0x112]=0;CHECK(ctr_fragment_decode(registers,defined,true,&decoded) && !decoded.write_mask);
    registers[0x100]=0;registers[0x102]=3;defined[0x101]=defined[0x103]=1;
    CHECK(ctr_fragment_decode(registers,defined,true,&decoded) && !decoded.blend_enabled && !memcmp(decoded.blend_constant,zero,4));
    defined[0x101]=15;registers[0x101]=5;decoded=before;
    CHECK(!ctr_fragment_decode(registers,defined,true,&decoded) && !memcmp(&decoded,&before,sizeof(decoded)));
}
static void blend_scenes(void) {
    ctr_scene_fixture fixture;ctr_scene_state state,before;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],original[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_fixture_background(original,sizeof(original));
    /* Original constant-TEV fragments blend with a seeded target. RGB uses
     * source alpha and its complement; alpha independently replaces its byte. */
    const uint8_t rgb[5][3]={{52,112,76},{36,80,27},{0,0,0},{8,16,24},{44,96,51}};
    for(unsigned bound=0;bound<2;++bound)for(unsigned equation=0;equation<5;++equation) {
        ctr_scene_fixture_make(&fixture,false);target_state(&state);memcpy(pixels,original,sizeof(pixels));
        state.registers[0xc0]=UINT32_C(0xe000e);state.registers[0xc3]=UINT32_C(0x7e69c35a);state.register_defined[0xc3]=15;
        state.registers[0x100]=0x100;state.registers[0x101]=blend_word(equation,0,6,7,1,0);state.register_defined[0x101]=15;
        state.registers[0x112]=15;
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        ctr_scene_result result=bound?ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
            UINT32_C(0x18010000),&report):execute(&fixture,&state,pixels,&report);
        CHECK(result==CTR_SCENE_OK && report.draws==2 && report.fragments==2304);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            uint8_t expected[]={16,32,48,255};
            if(x>=8 && x<56 && y>=8 && y<56) { memcpy(expected,rgb[equation],3);expected[3]=126; }
            CHECK(!memcmp(pixels+(y*64u+x)*4u,expected,4));
        }
        if(!equation) {
            /* Retained blending reads the committed prior target on the next
             * list, rather than replaying against its original background. */
            CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK && state.draws==4);
            const uint8_t second[]={70,153,90,126};
            for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)
                CHECK(!memcmp(pixels+(y*64u+x)*4u,second,4));
        }
    }
    const uint32_t invalid[]={5,1u<<3,1u<<11,15u<<16,15u<<28};
    for(unsigned bound=0;bound<2;++bound)for(unsigned index=0;index<sizeof(invalid)/sizeof(*invalid);++index) {
        ctr_scene_fixture_make(&fixture,false);target_state(&state);memcpy(pixels,original,sizeof(pixels));
        before=state;append(&fixture,0x101,invalid[index]);
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        ctr_scene_result result=bound?ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
            UINT32_C(0x18010000),&report):execute(&fixture,&state,pixels,&report);
        CHECK(result==CTR_SCENE_UNSUPPORTED && report.draws==2 && report.reg==0x101);
        CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(pixels,original,sizeof(pixels)));
    }
}
enum { DEPTH_COLOR_STRIDE=260,DEPTH_STRIDE=132,
       DEPTH_COLOR_BYTES=64*DEPTH_COLOR_STRIDE+12,DEPTH_BYTES=64*DEPTH_STRIDE+12 };
static void depth_state(ctr_scene_state *state,unsigned control) {
    target_state(state);
    const unsigned regs[]={0x04d,0x04e,0x06d,0x107,0x114,0x115,0x116,0x11c};
    const uint32_t values[]={UINT32_C(0xbf0000),0,1,control,2,2,0,UINT32_C(0x03004000)};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i) {
        state->registers[regs[i]]=values[i];state->register_defined[regs[i]]=15;
    }
}
static void depth_position(ctr_scene_fixture *fixture,uint32_t z) {
    for(unsigned vertex=0;vertex<4;++vertex)put32(fixture->resources+vertex*16u+8u,z);
}
static void depth_initial(uint8_t color[DEPTH_COLOR_BYTES],uint8_t depth[DEPTH_BYTES]) {
    static const unsigned stored[]={0,1,32766,32767,32768,65534,65535};
    memset(color,0x6d,DEPTH_COLOR_BYTES);memset(depth,0xa5,DEPTH_BYTES);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        static const uint8_t background[]={16,32,48,255};
        memcpy(color+y*DEPTH_COLOR_STRIDE+x*4u,background,4);
        unsigned z=stored[(x+3u*y)%7u];
        depth[y*DEPTH_STRIDE+x*2u]=(uint8_t)z;
        depth[y*DEPTH_STRIDE+x*2u+1u]=(uint8_t)(z>>8);
    }
}
static ctr_scene_result depth_execute(ctr_scene_fixture *fixture,ctr_scene_state *state,
                                      uint8_t *color,uint8_t *depth,ctr_scene_report *report) {
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture->resources,fixture->resource_bytes,NULL,0};
    ctr_scene_surface surface={color,DEPTH_COLOR_BYTES,64,64,DEPTH_COLOR_STRIDE};
    ctr_scene_depth_surface z={depth,DEPTH_BYTES,64,64,DEPTH_STRIDE,UINT32_C(0x18020000),0};
    return ctr_scene_execute_targets(state,fixture->commands,fixture->command_bytes,&resources,
                                     &surface,UINT32_C(0x18010000),&z,report);
}
static bool depth_relation(unsigned function,unsigned incoming,unsigned stored) {
    const bool relation[]={false,true,incoming==stored,incoming!=stored,incoming<stored,
        incoming<=stored,incoming>stored,incoming>=stored};
    return relation[function];
}
static void depth_comparisons(void) {
    /* These independently chosen dyadics cover signed zero, just below/above
     * the first D16 quantum, the half-depth truncation and both endpoints.
     * In particular (1-2^-17)*65535 lies strictly between 65534 and 65535. */
    static const struct { uint32_t position;unsigned quantized; } values[]={
        {0,0},{UINT32_C(0x80000000),0},{UINT32_C(0xb7800000),0},
        {UINT32_C(0xb8000000),1},{UINT32_C(0xbf000000),32767},
        {UINT32_C(0xbf7fff80),65534},{UINT32_C(0xbf800000),65535}
    };
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],expected_color[DEPTH_COLOR_BYTES];
    uint8_t depth[DEPTH_BYTES],old_depth[DEPTH_BYTES],expected_depth[DEPTH_BYTES];
    depth_initial(old_color,old_depth);
    for(unsigned function=0;function<8;++function)for(unsigned number=0;number<sizeof(values)/sizeof(values[0]);++number)
        for(unsigned control=0;control<8;++control) {
        bool test=(control&1u)!=0u,write=(control&2u)!=0u,gate=(control&4u)!=0u;
        unsigned incoming=values[number].quantized;
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,values[number].position);
        depth_state(&state,0xf00u|(function<<4)|(test?1u:0u)|(write?0x1000u:0u));
        state.registers[0x115]=gate?2u:0u;
        memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        memcpy(expected_color,old_color,sizeof(color));memcpy(expected_depth,old_depth,sizeof(depth));
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            size_t at=y*DEPTH_STRIDE+x*2u;
            unsigned stored=old_depth[at]|(unsigned)old_depth[at+1u]<<8;
            bool pass=!test || depth_relation(function,incoming,stored);
            if(pass) {
                const uint8_t pixel[]={(uint8_t)(x+y<63u?255u:0u),(uint8_t)(x+y<63u?0u:255u),0,255};
                memcpy(expected_color+y*DEPTH_COLOR_STRIDE+x*4u,pixel,4);
                if(write && gate) {
                    expected_depth[at]=(uint8_t)incoming;expected_depth[at+1u]=(uint8_t)(incoming>>8);
                }
            }
        }
        ctr_scene_result actual=depth_execute(&fixture,&state,color,depth,&report);
        if(actual!=CTR_SCENE_OK)fprintf(stderr,"D16 function=%u value=%u control=%u: %s reg=%x\n",
            function,number,control,ctr_scene_result_name(actual),report.reg);
        CHECK(actual==CTR_SCENE_OK && report.draws==2 && report.triangles==2 && report.fragments==2304);
        CHECK(!memcmp(color,expected_color,sizeof(color)));
        CHECK(!memcmp(depth,expected_depth,sizeof(depth)));
        CHECK(state.draws==2 && state.fragments==2304 && state.lists==1);
    }
    /* Alpha failure precedes the otherwise unconditional depth write. */
    ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1f00);
    state.registers[0x104]=1;memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
    CHECK(depth_execute(&fixture,&state,color,depth,&report)==CTR_SCENE_OK && report.fragments==2304);
    CHECK(!memcmp(color,old_color,sizeof(color)) && !memcmp(depth,old_depth,sizeof(depth)));
    /* A masked color target still allows a depth-only prepass. Depth gates use
     * 2, independently of the color gate's 15 and channel write mask. */
    ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1000);
    state.registers[0x114]=0;memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
    CHECK(depth_execute(&fixture,&state,color,depth,&report)==CTR_SCENE_OK);
    CHECK(!memcmp(color,old_color,sizeof(color)));
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        size_t at=y*DEPTH_STRIDE+x*2u;bool covered=x>=8 && x<56 && y>=8 && y<56;
        CHECK(depth[at]==(covered?255u:old_depth[at]) && depth[at+1u]==(covered?127u:old_depth[at+1u]));
    }
    /* Retained state and shader storage supply a configuration-free second
     * list. Disable depth writes; equality must read committed D16 bytes. */
    fixture.command_bytes=0;append(&fixture,0x107,0xf21);append(&fixture,0x114,2);append(&fixture,0x22f,1);
    CHECK(depth_execute(&fixture,&state,color,depth,&report)==CTR_SCENE_OK && report.draws==1 && state.lists==2);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        const uint8_t green[]={0,255,0,255};bool covered=x>=8 && x<56 && y>=8 && y<56 && x+y>=63u;
        CHECK(!memcmp(color+y*DEPTH_COLOR_STRIDE+x*4u,
                      covered?green:old_color+y*DEPTH_COLOR_STRIDE+x*4u,4));
    }
}
static void depth_overdraw(void) {
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],depth[DEPTH_BYTES];
    uint8_t old_depth[DEPTH_BYTES];depth_initial(old_color,old_depth);
    for(unsigned kind=0;kind<4;++kind) {
        ctr_scene_fixture_make(&fixture,false);
        uint8_t positions[48];memcpy(positions,fixture.resources,sizeof(positions));
        memcpy(fixture.resources,positions,sizeof(positions));memcpy(fixture.resources+48u,positions,sizeof(positions));
        const uint8_t order[]={0,1,2,3,4,5};memcpy(fixture.resources+96u,order,sizeof(order));fixture.resource_bytes=102;
        for(unsigned vertex=0;vertex<6;++vertex)put32(fixture.resources+vertex*16u+8u,
            vertex<3u?UINT32_C(0xbf000000):kind==0u?UINT32_C(0xbe800000):
            kind==1u?UINT32_C(0xbf400000):UINT32_C(0xbf000000));
        put32(fixture.commands+find(&fixture,0x227,0),96);put32(fixture.commands+find(&fixture,0x227,1),99);
        depth_state(&state,kind>=2u?0x1000u:0x1f41u);
        if(kind>=2u) {
            insert(&fixture,find(&fixture,0x22f,1),0x107,kind==2u?0xf21u:0xf41u);finish(&fixture);
        }
        memcpy(color,old_color,sizeof(color));memset(depth,255,sizeof(depth));
        CHECK(depth_execute(&fixture,&state,color,depth,&report)==CTR_SCENE_OK);
        CHECK(report.fragments==2256 && report.draws==2 && report.triangles==2);
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            bool covered=x>=8 && x<56 && y>=8 && y<56 && x+y<63u;
            uint8_t pixel[4];memcpy(pixel,old_color+y*DEPTH_COLOR_STRIDE+x*4u,4);
            if(covered && kind!=3u) {
                pixel[0]=kind==1u?255u:0u;pixel[1]=kind==1u?0u:255u;pixel[2]=0;pixel[3]=255;
            }
            CHECK(!memcmp(color+y*DEPTH_COLOR_STRIDE+x*4u,pixel,4));
            size_t at=y*DEPTH_STRIDE+x*2u;
            unsigned expected=!covered?65535u:kind==0u?16383u:32767u;
            CHECK((depth[at]|(unsigned)depth[at+1u]<<8)==expected);
        }
    }
}
static void depth_quantization(void) {
    /* An 8x8 target contains one analytic two-pixel-square draw. The first
     * triangle covers (0,0) and the second covers its complement; only depth
     * writes are enabled. Exercise every normal float24 exponent admitted by
     * clip Z and seven significand boundaries without rendering large images.
     * Binary64 exactly represents this <=33-significant-bit dyadic product;
     * truncating it provides a numeric oracle separate from integer bit shifts. */
    static const unsigned fractions[]={0,1,2,32767,32768,65534,65535};
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[8u*8u*4u],depth[8u*8u*2u];
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,0,NULL,0};
    ctr_scene_surface surface={color,sizeof(color),8,8,32};
    ctr_scene_depth_surface z={depth,sizeof(depth),8,8,16,UINT32_C(0x18020000),0};
    unsigned cases=0;
    for(unsigned exponent=1;exponent<=63;++exponent)
        for(unsigned index=0;index<sizeof(fractions)/sizeof(fractions[0]);++index) {
        unsigned fraction=fractions[index];
        if(exponent==63u && fraction)continue;
        double magnitude=ldexp(1.0+(double)fraction/65536.0,(int)exponent-63);
        unsigned expected=(unsigned)(magnitude*65535.0);
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,bits((float)-magnitude));
        for(unsigned vertex=0;vertex<4;++vertex) {
            put32(fixture.resources+vertex*16u,bits(vertex&1u?-0.5f:-1.0f));
            put32(fixture.resources+vertex*16u+4u,bits(vertex&2u?-0.5f:-1.0f));
        }
        set(&fixture,0x041,UINT32_C(0x410000));set(&fixture,0x043,UINT32_C(0x410000));
        depth_state(&state,0x1000);state.registers[0x11e]=UINT32_C(0x01007008);
        memset(color,0xa6,sizeof(color));memset(depth,0x39,sizeof(depth));resources.size=fixture.resource_bytes;
        CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,
              &surface,UINT32_C(0x18010000),&z,&report)==CTR_SCENE_OK);
        CHECK(report.fragments==4 && report.draws==2);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned stored=depth[(y*8u+x)*2u]|(unsigned)depth[(y*8u+x)*2u+1u]<<8;
            CHECK(stored==(x<2u && y<2u?expected:0x3939u));
        }
        for(unsigned byte=0;byte<sizeof(color);++byte)CHECK(color[byte]==0xa6u);
        ++cases;
    }
    CHECK(cases==435);
}
static void depth_rollback(void) {
    ctr_scene_fixture fixture;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],depth[DEPTH_BYTES],old_depth[DEPTH_BYTES];
    depth_initial(old_color,old_depth);
    const unsigned regs[]={0x114,0x115,0x116,0x11c,0x04d,0x04e,0x06d,0x107};
    const uint32_t invalid[]={1,3,2,UINT32_C(0x10000000),UINT32_C(0x3f0000),UINT32_C(0x3f0000),0,4};
    for(unsigned kind=0;kind<sizeof(regs)/sizeof(*regs);++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1f11);previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));append(&fixture,regs[kind],invalid[kind]);
        CHECK(depth_execute(&fixture,&state,color,depth,&report)!=CTR_SCENE_OK);
        CHECK(report.draws==2 && report.reg==regs[kind]);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) &&
              !memcmp(depth,old_depth,sizeof(depth)));
    }
    /* Every required depth register must be completely defined at the actual
     * draw. Disabled gates do not turn malformed state into an implicit default. */
    for(unsigned kind=0;kind<sizeof(regs)/sizeof(*regs);++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1f11);state.register_defined[regs[kind]]=1;previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        CHECK(depth_execute(&fixture,&state,color,depth,&report)!=CTR_SCENE_OK && !report.draws);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) &&
              !memcmp(depth,old_depth,sizeof(depth)));
    }
    /* Varying Z and non-float24 geometry fail after a valid first primitive;
     * neither the staged color nor its paired depth may leak through. */
    for(unsigned kind=0;kind<2;++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1f11);previous=state;
        put32(fixture.resources+3u*16u+8u,kind?UINT32_C(0xbf000001):UINT32_C(0xbe800000));
        memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        CHECK(depth_execute(&fixture,&state,color,depth,&report)!=CTR_SCENE_OK && report.draws==1);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) &&
              !memcmp(depth,old_depth,sizeof(depth)));
    }
    for(unsigned kind=0;kind<15;++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_state(&state,0x1f11);previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={color,sizeof(color),64,64,DEPTH_COLOR_STRIDE};
        ctr_scene_depth_surface z={depth,sizeof(depth),64,64,DEPTH_STRIDE,UINT32_C(0x18020000),0};
        ctr_scene_report *output=&report;
        if(kind==0)z.bytes=color;
        if(kind==1)z.bytes=&state;
        if(kind==2)z.bytes=&report;
        if(kind==3)z.bytes=fixture.resources;
        if(kind==4)z.bytes=fixture.commands;
        if(kind==5)z.bytes=&surface;
        if(kind==6)z.bytes=&z;
        if(kind==7)z.stride=127;
        if(kind==8)z.size=64u*127u;
        if(kind==9)z.width=63;
        if(kind==10)z.height=63;
        if(kind==11)z.address=UINT32_C(0x18010000);
        if(kind==12)z.address=UINT32_C(0x18000000);
        if(kind==13)z.bytes=(void *)(UINTPTR_MAX-3u);
        if(kind==14)output=(ctr_scene_report *)depth;
        CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&z,output)!=CTR_SCENE_OK);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) &&
              !memcmp(depth,old_depth,sizeof(depth)));
    }
}
typedef struct {
    const uint8_t *color,*depth;
    unsigned begins,draws,ends,aborts,fail;
    size_t color_corruption,depth_corruption;
} depth_backend;
static bool depth_backend_legacy_begin(void *context,const ctr_scene_surface *initial) {
    (void)context;(void)initial;CHECK(false);return false;
}
static bool depth_backend_legacy_end(void *context,const ctr_scene_surface *readback) {
    (void)context;(void)readback;CHECK(false);return false;
}
static bool depth_backend_begin(void *context,const ctr_scene_surface *initial,const ctr_scene_depth_surface *depth) {
    depth_backend *backend=context;++backend->begins;
    CHECK(initial->size==DEPTH_COLOR_BYTES && initial->stride==DEPTH_COLOR_STRIDE);
    CHECK(depth->size==DEPTH_BYTES && depth->stride==DEPTH_STRIDE && depth->address==UINT32_C(0x18020000));
    return backend->fail!=1u;
}
static bool depth_backend_draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                                unsigned count,const ctr_scene_texture *texture) {
    depth_backend *backend=context;++backend->draws;(void)vertices;
    CHECK(count==3 && !texture && state->registers[0x11c]==UINT32_C(0x03004000));
    return backend->fail!=2u;
}
static bool depth_backend_end(void *context,const ctr_scene_surface *readback,const ctr_scene_depth_surface *depth) {
    depth_backend *backend=context;++backend->ends;
    memcpy(readback->bytes,backend->color,readback->size);memcpy(depth->bytes,backend->depth,depth->size);
    if(backend->color_corruption)((uint8_t *)readback->bytes)[backend->color_corruption]^=1u;
    if(backend->depth_corruption)((uint8_t *)depth->bytes)[backend->depth_corruption]^=1u;
    return backend->fail!=3u;
}
static void depth_backend_abort(void *context) { depth_backend *backend=context;++backend->aborts; }
static void depth_backend_parity(void) {
    ctr_scene_fixture fixture;ctr_scene_fixture_make(&fixture,false);
    depth_position(&fixture,UINT32_C(0xbf800000));
    ctr_scene_state state,initial,expected;depth_state(&initial,0x1f11);expected=initial;
    ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],reference_color[DEPTH_COLOR_BYTES];
    uint8_t depth[DEPTH_BYTES],old_depth[DEPTH_BYTES],reference_depth[DEPTH_BYTES];
    depth_initial(old_color,old_depth);memcpy(reference_color,old_color,sizeof(old_color));
    memcpy(reference_depth,old_depth,sizeof(old_depth));
    CHECK(depth_execute(&fixture,&expected,reference_color,reference_depth,&report)==CTR_SCENE_OK);
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
    ctr_scene_surface surface={color,sizeof(color),64,64,DEPTH_COLOR_STRIDE};
    ctr_scene_depth_surface z={depth,sizeof(depth),64,64,DEPTH_STRIDE,UINT32_C(0x18020000),0};
    /* A correct color image cannot authorize a corrupted depth image, even if
     * the only changed byte is row padding or the final byte after all rows. */
    for(unsigned kind=0;kind<9;++kind) {
        state=initial;memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        depth_backend context={reference_color,reference_depth,0,0,0,0,kind>=4u && kind<=6u?kind-3u:0u,
            kind==1u?256u:0u,kind==2u?128u:kind==3u?sizeof(depth)-1u:0u};
        ctr_scene_backend backend={&context,depth_backend_legacy_begin,depth_backend_draw,depth_backend_legacy_end,
            depth_backend_abort,depth_backend_begin,depth_backend_end,NULL};
        if(kind==7u)backend.begin_depth=NULL;
        if(kind==8u)backend.end_depth=NULL;
        ctr_scene_result result=ctr_scene_execute_targets_backend(&state,fixture.commands,fixture.command_bytes,
            &resources,&surface,UINT32_C(0x18010000),&z,&backend,&report);
        CHECK(result==(kind>=7u?CTR_SCENE_UNSUPPORTED:kind?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        CHECK(!memcmp(&state,kind?&initial:&expected,sizeof(state)));
        CHECK(!memcmp(color,kind?old_color:reference_color,sizeof(color)));
        CHECK(!memcmp(depth,kind?old_depth:reference_depth,sizeof(depth)));
        if(kind>=7u)CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        else CHECK(context.begins==1 && context.aborts==(kind?1u:0u));
        if(kind>=1u && kind<=3u)CHECK(report.backend_stage==CTR_SCENE_BACKEND_PARITY);
    }
    /* Fractional software depth is explicitly outside native admission and
     * must reject before any native allocation, upload or callback. */
    for(unsigned kind=0;kind<3;++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,kind?UINT32_C(0xbf800000):UINT32_C(0xbf000000));
        if(kind==1u)append(&fixture,0x114,1);
        if(kind==2u)put32(fixture.resources+3u*16u+8u,0);
        state=initial;memcpy(color,old_color,sizeof(color));memcpy(depth,old_depth,sizeof(depth));
        depth_backend context={reference_color,reference_depth,0,0,0,0,0,0,0};
        ctr_scene_backend backend={&context,depth_backend_legacy_begin,depth_backend_draw,depth_backend_legacy_end,
            depth_backend_abort,depth_backend_begin,depth_backend_end,NULL};
        CHECK(ctr_scene_execute_targets_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&z,&backend,&report)==CTR_SCENE_UNSUPPORTED);
        CHECK(report.draws==(kind==1u?2u:kind==2u?1u:0u));
        CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        CHECK(!memcmp(&state,&initial,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) && !memcmp(depth,old_depth,sizeof(depth)));
    }
}
/* Original stencil references use mathematical byte operations independently
 * of the renderer. Every stored byte is exercised, with finite mask/ref sets. */
static unsigned stencil_reference(unsigned action,unsigned old,unsigned reference,unsigned mask) {
    const unsigned candidates[]={old,0,reference,old==255u?255u:old+1u,
        old?old-1u:0u,255u-old,(old+1u)%256u,(old+255u)%256u};
    unsigned result=0;
    for(unsigned bit=0;bit<8;++bit)result|=((mask&(1u<<bit))?candidates[action]:old)&(1u<<bit);
    return result;
}
static uint32_t stencil_word(unsigned function,unsigned write,unsigned reference,unsigned input) {
    return 1u|(function<<4)|(write<<8)|(reference<<16)|(input<<24);
}
static void stencil_contract(void) {
    uint32_t registers[CTR_PICA_REGISTER_COUNT]={0};uint8_t defined[CTR_PICA_REGISTER_COUNT]={0};
    ctr_stencil_state decoded,previous;
    static const unsigned masks[]={0,0x55,0xaa,255},refs[]={0,0xa6,255};
    for(unsigned bit=0;bit<32;++bit) {
        uint32_t one=UINT32_C(1)<<bit;
        CHECK(ctr_stencil_register_value(0x105,one)==((one&UINT32_C(0xffffff71))!=0u));
        CHECK(ctr_stencil_register_value(0x106,one)==((one&UINT32_C(0x777))!=0u));
    }
    for(unsigned function=0;function<8;++function)for(unsigned mask=0;mask<4;++mask)
        for(unsigned reference=0;reference<3;++reference) {
        ctr_stencil_state state={.enabled=true,.function=(uint8_t)function,
            .write_mask=(uint8_t)masks[mask],.reference=(uint8_t)refs[reference],.input_mask=(uint8_t)masks[mask]};
        for(unsigned stored=0;stored<256;++stored) {
            CHECK(ctr_stencil_passes(&state,(uint8_t)stored)==
                  depth_relation(function,refs[reference]&masks[mask],stored&masks[mask]));
            if(!function)for(unsigned action=0;action<8;++action)
                CHECK(ctr_stencil_apply(&state,action,(uint8_t)stored)==
                      stencil_reference(action,stored,refs[reference],masks[mask]));
        }
        state.enabled=false;CHECK(ctr_stencil_passes(&state,0) && ctr_stencil_passes(&state,255));
    }
    registers[0x105]=stencil_word(6,0x5a,0xa6,0x3c);registers[0x106]=0x765;
    defined[0x105]=defined[0x106]=15;
    CHECK(ctr_stencil_decode(registers,defined,true,&decoded));
    CHECK(decoded.enabled && decoded.function==6 && decoded.write_mask==0x5a &&
          decoded.reference==0xa6 && decoded.input_mask==0x3c &&
          decoded.action[0]==5 && decoded.action[1]==6 && decoded.action[2]==7);
    const unsigned regs[]={0x105,0x106};
    for(unsigned index=0;index<2;++index)for(unsigned mask=0;mask<15;++mask) {
        uint8_t old=defined[regs[index]];defined[regs[index]]=(uint8_t)mask;
        memset(&decoded,0xa5,sizeof(decoded));decoded.enabled=true;previous=decoded;
        CHECK(!ctr_stencil_decode(registers,defined,true,&decoded));
        CHECK(!memcmp(&decoded,&previous,sizeof(decoded)));defined[regs[index]]=old;
    }
    memset(&decoded,0xa5,sizeof(decoded));decoded.enabled=true;previous=decoded;
    CHECK(!ctr_stencil_decode(registers,defined,false,&decoded));CHECK(!memcmp(&decoded,&previous,sizeof(decoded)));
    registers[0x105]=registers[0x106]=0;
    CHECK(ctr_stencil_decode(registers,defined,false,&decoded) && !decoded.enabled);
}
enum { STENCIL_STRIDE=260,STENCIL_BYTES=64*STENCIL_STRIDE+12 };
static unsigned stencil_seed(unsigned x,unsigned y) { return (17u*x+31u*y)&255u; }
static void stencil_state(ctr_scene_state *state,unsigned control,unsigned config,unsigned actions) {
    depth_state(state,control);state->registers[0x105]=config;state->registers[0x106]=actions;
    state->registers[0x114]=state->registers[0x115]=state->registers[0x116]=3;
}
static void stencil_initial(uint8_t color[DEPTH_COLOR_BYTES],uint8_t packed[STENCIL_BYTES]) {
    static const unsigned z[]={0,1,0x12abcd,0x7ffffe,0x7fffff,0x800000,0xfffffe,0xffffff};
    memset(color,0x6d,DEPTH_COLOR_BYTES);memset(packed,0xa5,STENCIL_BYTES);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        const uint8_t background[]={16,32,48,255};memcpy(color+y*DEPTH_COLOR_STRIDE+x*4u,background,4);
        put32(packed+y*STENCIL_STRIDE+x*4u,z[(x+3u*y)&7u]|(stencil_seed(x,y)<<24));
    }
}
static ctr_scene_result stencil_execute(ctr_scene_fixture *fixture,ctr_scene_state *state,
                                        uint8_t *color,uint8_t *packed,ctr_scene_report *report) {
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture->resources,fixture->resource_bytes,NULL,0};
    ctr_scene_surface surface={color,DEPTH_COLOR_BYTES,64,64,DEPTH_COLOR_STRIDE};
    ctr_scene_depth_surface z={packed,STENCIL_BYTES,64,64,STENCIL_STRIDE,UINT32_C(0x18020000),3};
    return ctr_scene_execute_targets(state,fixture->commands,fixture->command_bytes,&resources,
                                     &surface,UINT32_C(0x18010000),&z,report);
}
static void stencil_pixel(uint8_t *color,unsigned x,unsigned y) {
    const uint8_t pixel[]={(uint8_t)(x+y<63u?255u:0u),(uint8_t)(x+y<63u?0u:255u),0,255};
    memcpy(color+y*DEPTH_COLOR_STRIDE+x*4u,pixel,4);
}
static void stencil_images(void) {
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],expected_color[DEPTH_COLOR_BYTES];
    uint8_t packed[STENCIL_BYTES],old_packed[STENCIL_BYTES],expected_packed[STENCIL_BYTES];
    stencil_initial(old_color,old_packed);bool seen[256]={false};
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)seen[stencil_seed(x,y)]=true;
    for(unsigned value=0;value<256;++value)CHECK(seen[value]);
    /* Force each branch independently. Other branches carry distinct actions,
     * so confusing stencil-fail, depth-fail and pass cannot accidentally pass. */
    for(unsigned outcome=0;outcome<3;++outcome)for(unsigned action=0;action<8;++action) {
        unsigned choices[]={(action+1u)%8u,(action+2u)%8u,(action+3u)%8u};choices[outcome]=action;
        unsigned actions=choices[0]|(choices[1]<<4)|(choices[2]<<8);
        ctr_scene_fixture_make(&fixture,false);
        stencil_state(&state,outcome==1u?0x1f01u:0x1f11u,stencil_word(outcome?1u:0u,0x5a,0xa6,255),actions);
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        memcpy(expected_color,old_color,sizeof(color));memcpy(expected_packed,old_packed,sizeof(packed));
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            size_t at=y*STENCIL_STRIDE+x*4u;unsigned word=get32(old_packed+at);
            unsigned next=stencil_reference(action,word>>24,0xa6,0x5a);
            put32(expected_packed+at,(next<<24)|(outcome==2u?0x7fffffu:word&0xffffffu));
            if(outcome==2u)stencil_pixel(expected_color,x,y);
        }
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK && report.fragments==2304);
        CHECK(!memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
    }
    /* Comparisons use masked reference on the left, not the stored byte.
     * Gate zero suppresses every selected stencil action and depth write, while
     * comparisons still select color fragments. */
    static const unsigned masks[]={0,15,240,255},refs[]={0x16,0xa6,255};
    for(unsigned function=0;function<8;++function)for(unsigned mask=0;mask<4;++mask)
        for(unsigned reference=0;reference<3;++reference) {
        ctr_scene_fixture_make(&fixture,false);
        stencil_state(&state,0x1f11,stencil_word(function,255,refs[reference],masks[mask]),0x222);
        state.registers[0x115]=0;
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        memcpy(expected_color,old_color,sizeof(color));
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)
            if(depth_relation(function,refs[reference]&masks[mask],stencil_seed(x,y)&masks[mask]))
                stencil_pixel(expected_color,x,y);
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK && report.draws==2);
        CHECK(!memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,old_packed,sizeof(packed)));
    }
}
static void stencil_depth_comparisons(void) {
    static const uint32_t positions[]={0,UINT32_C(0xbe800000),UINT32_C(0xbf000000),UINT32_C(0xbf800000)};
    static const unsigned incoming[]={0,0x3fffff,0x7fffff,0xffffff};
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],expected_color[DEPTH_COLOR_BYTES];
    uint8_t packed[STENCIL_BYTES],old_packed[STENCIL_BYTES],expected_packed[STENCIL_BYTES];
    stencil_initial(old_color,old_packed);
    /* Numerical depth comparisons read exactly the low 24 little-endian bits.
     * The independently varying high byte selects a distinct depth-fail action. */
    for(unsigned function=0;function<8;++function)for(unsigned anchor=0;anchor<4;++anchor) {
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,positions[anchor]);
        stencil_state(&state,0x1f01u|(function<<4),stencil_word(1,255,0xa6,255),0x251);
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        memcpy(expected_color,old_color,sizeof(color));memcpy(expected_packed,old_packed,sizeof(packed));
        for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            size_t at=y*STENCIL_STRIDE+x*4u;unsigned old=get32(old_packed+at);
            bool pass=depth_relation(function,incoming[anchor],old&0xffffffu);
            put32(expected_packed+at,pass?(UINT32_C(0xa6000000)|incoming[anchor]):
                  (((255u-(old>>24))<<24)|(old&0xffffffu)));
            if(pass)stencil_pixel(expected_color,x,y);
        }
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK &&
              report.fragments==2304 && report.draws==2);
        CHECK(!memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
    }
}
static void stencil_order_and_retention(void) {
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],expected_color[DEPTH_COLOR_BYTES];
    uint8_t packed[STENCIL_BYTES],old_packed[STENCIL_BYTES],expected_packed[STENCIL_BYTES];
    stencil_initial(old_color,old_packed);
    for(unsigned kind=0;kind<5;++kind) {
        ctr_scene_fixture_make(&fixture,false);
        stencil_state(&state,kind==1u?0x1f01u:kind==2u?0x1000u:0x1f11u,
            stencil_word(kind==0u?0u:1u,255,0xa6,255),0x235);
        if(kind==0u)state.registers[0x104]=1; /* NEVER alpha before stencil-fail INVERT. */
        if(kind==2u)state.registers[0x114]=3; /* Depth disabled: stencil pass still runs. */
        if(kind==3u)state.registers[0x105]&=~1u; /* Disabled stencil must preserve its byte. */
        if(kind==4u)state.registers[0x107]&=~0x1000u; /* Stencil-only update preserves depth. */
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        memcpy(expected_color,old_color,sizeof(color));memcpy(expected_packed,old_packed,sizeof(packed));
        if(kind)for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
            size_t at=y*STENCIL_STRIDE+x*4u;unsigned old=get32(old_packed+at);
            unsigned next=kind==1u?stencil_reference(3,old>>24,0xa6,255):kind==3u?old>>24:0xa6;
            unsigned z=kind==1u || kind==4u?old&0xffffffu:0x7fffffu;
            put32(expected_packed+at,(next<<24)|z);
            if(kind!=1u && kind!=2u)stencil_pixel(expected_color,x,y);
        }
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK && report.fragments==2304);
        CHECK(!memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
    }
    /* A stencil-only prepass retains per-pixel data for a later list. It changes
     * only the lower stencil nibble, preserving depth and color exactly. */
    ctr_scene_fixture_make(&fixture,false);stencil_state(&state,0,stencil_word(1,15,0x4c,255),0x200);
    memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
    memcpy(expected_packed,old_packed,sizeof(packed));
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
        size_t at=y*STENCIL_STRIDE+x*4u;unsigned old=get32(old_packed+at);
        put32(expected_packed+at,(old&UINT32_C(0xf0ffffff))|UINT32_C(0x0c000000));
    }
    CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK);
    CHECK(!memcmp(color,old_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
    fixture.command_bytes=0;append(&fixture,0x105,stencil_word(2,0,0x0c,15));append(&fixture,0x107,0xf00);
    append(&fixture,0x115,0);append(&fixture,0x22f,1);
    memcpy(expected_color,old_color,sizeof(color));
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)if(x+y>=63u) {
        const uint8_t green[]={0,255,0,255};memcpy(expected_color+y*DEPTH_COLOR_STRIDE+x*4u,green,4);
    }
    CHECK(stencil_execute(&fixture,&state,color,packed,&report)==CTR_SCENE_OK && state.lists==2 && report.draws==1);
    CHECK(!memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
}
static void stencil_quantization(void) {
    /* Binary64 evaluates this finite dyadic product exactly (<=41 significant
     * bits), independently of the renderer's integer-significand shifts. */
    static const unsigned fractions[]={0,1,2,32767,32768,65534,65535};
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t color[8u*8u*4u],packed[8u*8u*4u];unsigned cases=0;
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,0,NULL,0};
    ctr_scene_surface surface={color,sizeof(color),8,8,32};
    ctr_scene_depth_surface z={packed,sizeof(packed),8,8,32,UINT32_C(0x18020000),3};
    for(unsigned exponent=1;exponent<=63;++exponent)for(unsigned index=0;index<7;++index) {
        unsigned fraction=fractions[index];if(exponent==63u && fraction)continue;
        double magnitude=ldexp(1.0+(double)fraction/65536.0,(int)exponent-63);
        unsigned expected=(unsigned)(magnitude*16777215.0);
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,bits((float)-magnitude));
        for(unsigned vertex=0;vertex<4;++vertex) {
            put32(fixture.resources+vertex*16u,bits(vertex&1u?-0.5f:-1.0f));
            put32(fixture.resources+vertex*16u+4u,bits(vertex&2u?-0.5f:-1.0f));
        }
        set(&fixture,0x041,UINT32_C(0x410000));set(&fixture,0x043,UINT32_C(0x410000));
        stencil_state(&state,0x1000,0,0);state.registers[0x114]=0;state.registers[0x11e]=UINT32_C(0x01007008);
        memset(color,0xa6,sizeof(color));for(unsigned i=0;i<64;++i)put32(packed+i*4u,UINT32_C(0x6d123456));
        resources.size=fixture.resource_bytes;
        CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,
              &surface,UINT32_C(0x18010000),&z,&report)==CTR_SCENE_OK && report.fragments==4);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            CHECK(get32(packed+(y*8u+x)*4u)==(UINT32_C(0x6d000000)|(x<2u && y<2u?expected:0x123456u)));
        for(unsigned byte=0;byte<sizeof(color);++byte)CHECK(color[byte]==0xa6u);
        ++cases;
    }
    CHECK(cases==435);
}
static void stencil_rollback(void) {
    ctr_scene_fixture fixture;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],packed[STENCIL_BYTES],old_packed[STENCIL_BYTES];
    stencil_initial(old_color,old_packed);
    const unsigned regs[]={0x105,0x106,0x106,0x114,0x115,0x116,0x105,0x107};
    const uint32_t invalid[]={4,8,UINT32_C(0x80000000),2,2,2,0x80,4};
    for(unsigned kind=0;kind<8;++kind) {
        ctr_scene_fixture_make(&fixture,false);stencil_state(&state,0x1f11,stencil_word(1,255,0xa6,255),0x222);previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));append(&fixture,regs[kind],invalid[kind]);
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)!=CTR_SCENE_OK && report.draws==2 && report.reg==regs[kind]);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) && !memcmp(packed,old_packed,sizeof(packed)));
    }
    for(unsigned reg=0x105;reg<=0x106;++reg)for(unsigned partial=0;partial<4;++partial) {
        const unsigned masks[]={0,1,7,14};ctr_scene_fixture_make(&fixture,false);
        stencil_state(&state,0x1f11,0,0);state.register_defined[reg]=(uint8_t)masks[partial];previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        CHECK(stencil_execute(&fixture,&state,color,packed,&report)!=CTR_SCENE_OK && !report.draws);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) && !memcmp(packed,old_packed,sizeof(packed)));
    }
    for(unsigned kind=0;kind<8;++kind) {
        ctr_scene_fixture_make(&fixture,false);stencil_state(&state,0x1f11,stencil_word(1,255,0xa6,255),0x222);previous=state;
        memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={color,sizeof(color),64,64,DEPTH_COLOR_STRIDE};
        ctr_scene_depth_surface z={packed,sizeof(packed),64,64,STENCIL_STRIDE,UINT32_C(0x18020000),3};
        if(kind==0)z.bytes=color;
        if(kind==1)z.bytes=&report;
        if(kind==2)z.format=2;
        if(kind==3)z.format=4;
        if(kind==4)z.stride=255;
        if(kind==5)z.size=64u*255u;
        /* The upper half of a four-byte D24S8 target overlaps color. Treating
         * it as a two-byte D16 span would miss this physical alias. */
        if(kind==6)z.address=UINT32_C(0x1800d000);
        if(kind==7) {
            z.address=UINT32_C(0x1800d000);
            ctr_scene_resource_span span={UINT32_C(0x1800fff0),old_packed,16};
            resources.textures=&span;resources.texture_count=1;
            CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
                  UINT32_C(0x18100000),&z,&report)==CTR_SCENE_ALIAS);
        } else CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
                      UINT32_C(0x18010000),&z,&report)!=CTR_SCENE_OK);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) && !memcmp(packed,old_packed,sizeof(packed)));
    }
}
typedef struct {
    const uint8_t *color,*packed;
    unsigned begins,draws,ends,aborts,fail;
    size_t color_corruption,packed_corruption;
} stencil_backend;
static bool stencil_backend_begin(void *context,const ctr_scene_surface *initial,const ctr_scene_depth_surface *depth) {
    stencil_backend *b=context;++b->begins;
    CHECK(initial->size==DEPTH_COLOR_BYTES && depth->size==STENCIL_BYTES && depth->stride==STENCIL_STRIDE && depth->format==3);
    return b->fail!=1u;
}
static bool stencil_backend_draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                                  unsigned count,const ctr_scene_texture *texture) {
    stencil_backend *b=context;++b->draws;(void)vertices;
    CHECK(count==3 && !texture && state->registers[0x116]==3);return b->fail!=2u;
}
static bool stencil_backend_end(void *context,const ctr_scene_surface *readback,const ctr_scene_depth_surface *depth) {
    stencil_backend *b=context;++b->ends;
    memcpy(readback->bytes,b->color,readback->size);memcpy(depth->bytes,b->packed,depth->size);
    if(b->color_corruption)((uint8_t *)readback->bytes)[b->color_corruption]^=1u;
    if(b->packed_corruption)((uint8_t *)depth->bytes)[b->packed_corruption]^=1u;
    return b->fail!=3u;
}
static void stencil_backend_abort(void *context) { ++((stencil_backend*)context)->aborts; }
static void stencil_backend_parity(void) {
    ctr_scene_fixture fixture;ctr_scene_state state,initial,previous;ctr_scene_report report;
    uint8_t color[DEPTH_COLOR_BYTES],old_color[DEPTH_COLOR_BYTES],expected_color[DEPTH_COLOR_BYTES];
    uint8_t packed[STENCIL_BYTES],old_packed[STENCIL_BYTES],expected_packed[STENCIL_BYTES];
    stencil_initial(old_color,old_packed);memcpy(expected_color,old_color,sizeof(color));memcpy(expected_packed,old_packed,sizeof(packed));
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
        size_t at=y*STENCIL_STRIDE+x*4u;unsigned old=get32(old_packed+at);
        put32(expected_packed+at,(stencil_reference(2,old>>24,0xa6,0x5a)<<24)|0xffffffu);
        stencil_pixel(expected_color,x,y);
    }
    stencil_state(&initial,0x1f11,stencil_word(1,0x5a,0xa6,255),0x251);
    for(unsigned kind=0;kind<14;++kind) {
        ctr_scene_fixture_make(&fixture,false);depth_position(&fixture,kind==11u?UINT32_C(0xbf000000):UINT32_C(0xbf800000));
        if(kind==12u)append(&fixture,0x105,4);
        if(kind==13u)put32(fixture.resources+3u*16u+8u,0);
        state=initial;previous=state;memcpy(color,old_color,sizeof(color));memcpy(packed,old_packed,sizeof(packed));
        const size_t sample=8u*STENCIL_STRIDE+8u*4u;
        stencil_backend context={expected_color,expected_packed,0,0,0,0,
            kind>=6u && kind<=8u?kind-5u:0u,kind==1u?256u:0u,
            kind==2u?sample:kind==3u?sample+3u:kind==4u?256u:kind==5u?sizeof(packed)-1u:0u};
        ctr_scene_backend backend={&context,depth_backend_legacy_begin,stencil_backend_draw,depth_backend_legacy_end,
            stencil_backend_abort,stencil_backend_begin,stencil_backend_end,NULL};
        if(kind==9u)backend.begin_depth=NULL;
        if(kind==10u)backend.end_depth=NULL;
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={color,sizeof(color),64,64,DEPTH_COLOR_STRIDE};
        ctr_scene_depth_surface z={packed,sizeof(packed),64,64,STENCIL_STRIDE,UINT32_C(0x18020000),3};
        ctr_scene_result actual=ctr_scene_execute_targets_backend(&state,fixture.commands,fixture.command_bytes,&resources,
            &surface,UINT32_C(0x18010000),&z,&backend,&report);
        CHECK(actual==(kind>=9u?CTR_SCENE_UNSUPPORTED:kind?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        if(kind)CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,old_color,sizeof(color)) && !memcmp(packed,old_packed,sizeof(packed)));
        else CHECK(state.draws==2 && state.lists==1 && !memcmp(color,expected_color,sizeof(color)) && !memcmp(packed,expected_packed,sizeof(packed)));
        if(kind>=9u)CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        else CHECK(context.begins==1 && context.aborts==(kind?1u:0u));
        if(kind>=1u && kind<=5u)CHECK(report.backend_stage==CTR_SCENE_BACKEND_PARITY);
    }
}

static void stencil_capacity(void) {
    /* A full 512x512 D24S8 target is one MiB, independent of the retained
     * half-MiB D16 cap. Validate every color/packed byte at the exact boundary. */
    const size_t capacity=512u*512u*4u;
    uint8_t *color=malloc(capacity),*packed=malloc(capacity+1u);
    uint8_t *expected_color=malloc(capacity),*expected_packed=malloc(capacity+1u);
    CHECK(color && packed && expected_color && expected_packed);
    memset(color,0x6d,capacity);memset(packed,0x39,capacity+1u);
    memcpy(expected_color,color,capacity);memcpy(expected_packed,packed,capacity+1u);
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
        size_t at=(y*512u+x)*4u;
        const uint8_t pixel[]={(uint8_t)(x+y<63u?255u:0u),(uint8_t)(x+y<63u?0u:255u),0,255};
        memcpy(expected_color+at,pixel,4);put32(expected_packed+at,UINT32_C(0xa67fffff));
    }
    ctr_scene_fixture fixture;ctr_scene_fixture_make(&fixture,false);
    ctr_scene_state state,previous;ctr_scene_report report;
    stencil_state(&state,0x1f11,stencil_word(1,255,0xa6,255),0x222);
    state.registers[0x11d]=UINT32_C(0x18100000)/8u;
    state.registers[0x11c]=UINT32_C(0x18300000)/8u;
    state.registers[0x11e]=UINT32_C(0x011ff200);
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
    ctr_scene_surface surface={color,capacity,512,512,2048};
    ctr_scene_depth_surface z={packed,capacity,512,512,2048,UINT32_C(0x18300000),3};
    CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18100000),&z,&report)==CTR_SCENE_OK && report.fragments==2304);
    CHECK(!memcmp(color,expected_color,capacity) && !memcmp(packed,expected_packed,capacity+1u));
    previous=state;++z.size;
    CHECK(ctr_scene_execute_targets(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18100000),&z,&report)==CTR_SCENE_LIMIT);
    CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(color,expected_color,capacity) &&
          !memcmp(packed,expected_packed,capacity+1u));
    free(color);free(packed);free(expected_color);free(expected_packed);
}

/* Analytic untextured buffer scenes keep numeric TEV operation testing in the
 * independent texture/native references. These exercise real scene admission,
 * delayed publication, retained command-byte writes and transaction boundaries. */
static void buffer_state(ctr_scene_state *state) {
    target_state(state);
    state->register_defined[0x0e0]=state->register_defined[0x0fd]=15;
    state->registers[0x0fd]=UINT32_C(0x7b513719);
    for(unsigned stage=0;stage<6;++stage) {
        unsigned base=ctr_tev_registers[stage];
        state->register_defined[base+3u]=15;
        state->registers[base+3u]=UINT32_C(0x654f351f)+stage*UINT32_C(0x11111111);
    }
}
static void buffer_image(const uint8_t *pixels,size_t stride,const uint8_t *before,
                          const uint8_t color[4],bool primary_rgb) {
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)for(unsigned lane=0;lane<4;++lane) {
        size_t at=(size_t)y*stride+x*4u+lane;uint8_t expected=before[at];
        if(x>=8u && x<56u && y>=8u && y<56u) {
            expected=color[lane];
            if(primary_rgb && lane<3u)
                expected=lane==0u?(x+y<63u?255u:0u):lane==1u?(x+y<63u?0u:255u):0u;
        }
        CHECK(pixels[at]==expected);
    }
}
static void buffer_images(void) {
    static const uint8_t colors[][4]={
        {0,0,0,0}, {0x19,0x37,0x51,0x7b}, {0x19,0x37,0x51,0x7b},
        {0x19,0x37,0x51,0x7b}, {0x1f,0x35,0x4f,0x65},
        {0x1f,0x35,0x4f,0x7b}, {0x1f,0x35,0x4f,0x76},
        {0x52,0x68,0x82,0x98}, {0x30,0x46,0x60,0x76},
        {0x52,0x68,0x82,0x7b}
    };
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_fixture_background(before,sizeof(before));
    for(unsigned bound=0;bound<2;++bound)for(unsigned kind=0;kind<10;++kind) {
        ctr_scene_fixture_make(&fixture,false);buffer_state(&state);memcpy(pixels,before,sizeof(pixels));
        if(kind==0u)state.registers[0xc0]=UINT32_C(0x0d000d); /* Stage 0 sees zero. */
        if(kind==1u)state.registers[0xc8]=UINT32_C(0x0d000d); /* Stage 1 sees initial RGBA. */
        if(kind==2u)state.registers[0xd0]=UINT32_C(0x0d000d);
        if(kind>=3u && kind<=6u) {
            state.registers[0xc0]=UINT32_C(0x0e000e);
            state.registers[0x0e0]=kind<5u?UINT32_C(0x1100):UINT32_C(0x2100);
            if(kind==3u)state.registers[0xc8]=UINT32_C(0x0d000d); /* Not the stage 0 result yet. */
            else {
                state.registers[0xc8]=UINT32_C(0x0e000e);
                state.registers[kind==6u?0xd8:0xd0]=UINT32_C(0x0d000d);
            }
        }
        if(kind==7u || kind==9u) {
            state.registers[0xd8]=UINT32_C(0x0e000e);
            state.registers[0x0e0]=kind==7u?UINT32_C(0x8800):UINT32_C(0x0800);
            /* Stage 4 is passthrough, but must publish stage 3 for stage 5. */
            state.registers[0xf8]=UINT32_C(0x0d000d);
        }
        if(kind==8u) {
            state.registers[0xc8]=UINT32_C(0x0e000e);
            state.registers[0xd8]=UINT32_C(0x0e000e);
            /* Stage 2 is passthrough and still writes the stage 1 output. */
            state.registers[0x0e0]=UINT32_C(0x4400);state.registers[0xf0]=UINT32_C(0x0d000d);
        }
        ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
        ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
        ctr_scene_result result=bound?ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,
            &resources,&surface,UINT32_C(0x18010000),&report):execute(&fixture,&state,pixels,&report);
        CHECK(result==CTR_SCENE_OK && report.draws==2 && report.fragments==2304 && report.shader_instructions==18);
        buffer_image(pixels,256,before,colors[kind],false);
    }
}
static void buffer_retention(void) {
    ctr_scene_fixture fixture;ctr_scene_state state;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES];
    const uint8_t initial[]={0x19,0x37,0x51,0x7b},updated[]={0x19,0xd2,0x51,0xe1};
    const uint8_t alpha_only[]={0,0,0,0xe1};
    buffer_state(&state);state.register_defined[0x0e0]=state.register_defined[0x0fd]=0;
    state.registers[0x0fd]=0;state.registers[0xd0]=UINT32_C(0x0d000d);
    ctr_scene_fixture_make(&fixture,false);
    insert(&fixture,0,0x0e0,0);insert(&fixture,8,0x0fd,UINT32_C(0x7b513719));finish(&fixture);
    ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));
    CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK);
    CHECK(state.register_defined[0x0e0]==15 && state.register_defined[0x0fd]==15);
    buffer_image(pixels,256,before,initial,false);
    /* Complete retained definitions permit individual command-byte updates. */
    ctr_scene_fixture_make(&fixture,false);insert(&fixture,0,0x0e0,UINT32_C(0x100));
    put32(fixture.commands+4u,UINT32_C(0x20000)|0x0e0u);
    insert(&fixture,8,0x0fd,UINT32_C(0xe1000000));put32(fixture.commands+12u,UINT32_C(0x80000)|0x0fdu);finish(&fixture);
    CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK);
    CHECK(state.registers[0x0e0]==UINT32_C(0x100) && state.registers[0x0fd]==UINT32_C(0xe1513719));
    buffer_image(pixels,256,before,alpha_only,true);
    /* Closing the RGB update restores initial-buffer RGB; fragments/lists do
     * not carry the prior fragment's pending/visible values. */
    ctr_scene_fixture_make(&fixture,false);insert(&fixture,0,0x0e0,0);
    put32(fixture.commands+4u,UINT32_C(0x20000)|0x0e0u);
    insert(&fixture,8,0x0fd,UINT32_C(0xd200));put32(fixture.commands+12u,UINT32_C(0x20000)|0x0fdu);finish(&fixture);
    CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK);
    CHECK(state.registers[0x0e0]==0 && state.registers[0x0fd]==UINT32_C(0xe151d219));
    CHECK(state.lists==3 && state.draws==6 && state.fragments==6912 && state.shader_instructions==54);
    buffer_image(pixels,256,before,updated,false);
    /* Two partial fresh writes may finish the complete definition before a
     * draw; a single partial write is rejected in buffer_rollback below. */
    buffer_state(&state);state.register_defined[0x0e0]=state.register_defined[0x0fd]=0;
    state.registers[0x0e0]=state.registers[0x0fd]=0;state.registers[0xc8]=UINT32_C(0x0d000d);
    ctr_scene_fixture_make(&fixture,false);insert(&fixture,0,0x0fd,UINT32_C(0x00003719));
    put32(fixture.commands+4u,UINT32_C(0x30000)|0x0fdu);
    insert(&fixture,8,0x0fd,UINT32_C(0x7b510000));put32(fixture.commands+12u,UINT32_C(0xc0000)|0x0fdu);
    insert(&fixture,16,0x0e0,0);put32(fixture.commands+20u,UINT32_C(0x70000)|0x0e0u);
    insert(&fixture,24,0x0e0,0);put32(fixture.commands+28u,UINT32_C(0x80000)|0x0e0u);finish(&fixture);
    CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK);
    CHECK(state.register_defined[0x0fd]==15 && state.register_defined[0x0e0]==15);
    buffer_image(pixels,256,before,initial,false);
}
typedef struct {
    const uint8_t *expected;
    size_t size,corrupt;
    unsigned begins,draws,ends,aborts;
} buffer_backend;
static bool buffer_backend_begin(void *context,const ctr_scene_surface *initial) {
    buffer_backend *p=context;++p->begins;CHECK(initial->size==p->size);return true;
}
static bool buffer_backend_draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,
                                 unsigned count,const ctr_scene_texture *texture) {
    buffer_backend *p=context;++p->draws;CHECK(!texture && count==3 && vertices[0].enabled==3);
    CHECK(state->register_defined[0x0e0]==15 && state->register_defined[0x0fd]==15);return true;
}
static bool buffer_backend_end(void *context,const ctr_scene_surface *readback) {
    buffer_backend *p=context;++p->ends;CHECK(readback->size==p->size);
    memcpy(readback->bytes,p->expected,p->size);
    if(p->corrupt)((uint8_t *)readback->bytes)[p->corrupt]^=1u;
    return true;
}
static void buffer_backend_abort(void *context) { ++((buffer_backend*)context)->aborts; }
static void buffer_rollback(void) {
    ctr_scene_fixture fixture;ctr_scene_state state,previous;ctr_scene_report report;
    uint8_t pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,0,NULL,0};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};
    /* Even an inactive, zero-valued partial definition cannot inherit the
     * undefined bytes of a fresh register. Includes both bound/unbound APIs. */
    for(unsigned bound=0;bound<2;++bound)for(unsigned which=0;which<2;++which)for(unsigned mask=1;mask<15;++mask) {
        ctr_scene_fixture_make(&fixture,false);buffer_state(&state);unsigned reg=which?0x0fd:0x0e0;
        state.registers[0x0fd]=0;state.register_defined[reg]=0;
        insert(&fixture,0,reg,0);put32(fixture.commands+4u,(mask<<16)|reg);finish(&fixture);
        previous=state;ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));
        resources.size=fixture.resource_bytes;
        ctr_scene_result result=bound?ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,
            &resources,&surface,UINT32_C(0x18010000),&report):execute(&fixture,&state,pixels,&report);
        CHECK(result==CTR_SCENE_UNSUPPORTED && !report.draws);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    }
    /* Each activation path independently requires BOTH complete registers. */
    for(unsigned activation=0;activation<3;++activation)for(unsigned which=0;which<2;++which) {
        ctr_scene_fixture_make(&fixture,false);buffer_state(&state);state.registers[0x0fd]=0;
        if(activation==0u)state.registers[0xc8]=UINT32_C(0x0d000d);
        if(activation==1u)state.registers[0x0e0]=UINT32_C(0x100);
        if(activation==2u)state.registers[0x0fd]=UINT32_C(0x7b513719);
        state.register_defined[which?0x0fd:0x0e0]=0;previous=state;
        ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));resources.size=fixture.resource_bytes;
        buffer_backend context={before,sizeof(before),0,0,0,0,0};
        ctr_scene_backend backend={&context,buffer_backend_begin,buffer_backend_draw,buffer_backend_end,buffer_backend_abort,NULL,NULL,NULL};
        CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,
              &surface,UINT32_C(0x18010000),&backend,&report)==CTR_SCENE_UNSUPPORTED && !report.draws);
        CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    }
    /* The entirely implicit legacy path stays valid, while declaring a lone
     * partial buffer register must not bypass TEV admission through defaults. */
    for(unsigned which=0;which<2;++which) {
        ctr_scene_fixture_make(&fixture,false);ctr_scene_init(&state);insert(&fixture,0,which?0x0fd:0x0e0,0);
        put32(fixture.commands+4u,UINT32_C(0x10000)|(which?0x0fd:0x0e0));finish(&fixture);previous=state;
        ctr_scene_fixture_background(pixels,sizeof(pixels));memcpy(before,pixels,sizeof(before));
        CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_UNSUPPORTED && !report.draws);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    }
    /* Every reserved/fog bit remains a late-list error, after two staged
     * draws changed the candidate image. Nothing reaches backend callbacks. */
    for(unsigned retained=0;retained<2;++retained)for(unsigned bit=0;bit<32;++bit)if(bit<8u || bit>=16u) {
        buffer_state(&state);state.registers[0xc8]=UINT32_C(0x0d000d);ctr_scene_fixture_make(&fixture,false);
        ctr_scene_fixture_background(pixels,sizeof(pixels));
        if(retained)CHECK(execute(&fixture,&state,pixels,&report)==CTR_SCENE_OK);
        previous=state;memcpy(before,pixels,sizeof(before));
        ctr_scene_fixture_make(&fixture,false);insert(&fixture,0,0x0fd,UINT32_C(0x936b4527));
        append(&fixture,0x0e0,UINT32_C(0x1100)|(UINT32_C(1)<<bit));resources.size=fixture.resource_bytes;
        buffer_backend context={before,sizeof(before),0,0,0,0,0};
        ctr_scene_backend backend={&context,buffer_backend_begin,buffer_backend_draw,buffer_backend_end,buffer_backend_abort,NULL,NULL,NULL};
        CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,
              &surface,UINT32_C(0x18010000),&backend,&report)==CTR_SCENE_UNSUPPORTED);
        CHECK(report.draws==2 && report.fragments==2304 && report.reg==0x0e0);
        CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    }
}
static void buffer_backend_parity(void) {
    enum { STRIDE=260u,SIZE=64u*STRIDE+12u };
    ctr_scene_fixture fixture;ctr_scene_fixture_make(&fixture,false);
    ctr_scene_state initial,expected,state;buffer_state(&initial);initial.registers[0xc8]=UINT32_C(0x0d000d);
    uint8_t pixels[SIZE],before[SIZE],reference[SIZE];ctr_scene_fixture_background(before,sizeof(before));
    memcpy(reference,before,sizeof(reference));const uint8_t color[]={0x19,0x37,0x51,0x7b};
    for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x)memcpy(reference+y*STRIDE+x*4u,color,4);
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,fixture.resource_bytes,NULL,0};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,STRIDE};ctr_scene_report report;
    expected=initial;memcpy(pixels,before,sizeof(pixels));
    CHECK(ctr_scene_execute_target(&expected,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&report)==CTR_SCENE_OK && !memcmp(pixels,reference,sizeof(pixels)));
    for(unsigned kind=0;kind<4;++kind) {
        size_t corrupt=kind==1u?(16u*STRIDE+16u*4u):kind==2u?256u:kind==3u?SIZE-1u:0u;
        state=initial;memcpy(pixels,before,sizeof(pixels));buffer_backend context={reference,sizeof(reference),corrupt,0,0,0,0};
        ctr_scene_backend backend={&context,buffer_backend_begin,buffer_backend_draw,buffer_backend_end,buffer_backend_abort,NULL,NULL,NULL};
        CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)==(kind?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        CHECK(context.begins==1 && context.draws==2 && context.ends==1 && context.aborts==(kind?1u:0u));
        CHECK(!memcmp(pixels,kind?before:reference,sizeof(pixels)) && !memcmp(&state,kind?&initial:&expected,sizeof(state)));
        CHECK(report.backend_stage==(kind?CTR_SCENE_BACKEND_PARITY:CTR_SCENE_BACKEND_NONE));
    }
}

/* Original multiunit images: independently selected UV pairs and distinct
 * source patterns make a swapped binding or coordinate set observable. */
static const uint32_t material_addresses[3]={UINT32_C(0x18008000),UINT32_C(0x18009000),UINT32_C(0x1800a000)};
static uint8_t material_texel(unsigned unit,unsigned x,unsigned y,unsigned lane) {
    const unsigned base[4]={31u,73u,101u,137u},dx[4]={7u,5u,3u,2u},dy[4]={11u,3u,9u,4u};
    return (uint8_t)(base[lane]+unit*13u+x*dx[lane]+y*dy[lane]);
}
static void material_sources(uint8_t raw[3][256],ctr_scene_resource_span spans[3]) {
    for(unsigned unit=0;unit<3u;++unit) {
        for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x)for(unsigned lane=0;lane<4u;++lane)
            raw[unit][texture_tile(x,y,8u)*4u+3u-lane]=material_texel(unit,x,y,lane);
        spans[unit]=(ctr_scene_resource_span){material_addresses[unit],raw[unit],256u};
    }
}
static void material_fixture(ctr_scene_fixture *fixture,unsigned mask,unsigned coordinates,bool alternate,unsigned filters) {
    textured_fixture(fixture,false,0,(filters&1u)?6u:0u,0.25f,0.25f,0.75f,0.75f);
    set(fixture,0x082,UINT32_C(0x00080008));set(fixture,0x080,mask|(alternate?1u<<13:0u));
    set(fixture,0x2bd,31u);set(fixture,0x04f,5u);
    set(fixture,0x052,(coordinates&1u)?UINT32_C(0x1f1f0d0c):UINT32_C(0x1f1f1f1f));
    set(fixture,0x064,coordinates?1u:0u);set(fixture,0x06f,3u|(coordinates<<8));
    size_t at=find(fixture,0x2cc,3);
    insert(fixture,at,0x2cc,mov(32,3,0));insert(fixture,at+8u,0x2cc,mov(33,4,0));
    at=find(fixture,0x232,0);
    insert(fixture,at,0x053,(coordinates&2u)?UINT32_C(0x1f1f0f0e):UINT32_C(0x1f1f1f1f));at+=8u;
    insert(fixture,at,0x054,(coordinates&4u)?UINT32_C(0x1f1f1716):UINT32_C(0x1f1f1f1f));at+=8u;
    for(unsigned unit=1;unit<3u;++unit) {
        const unsigned *registers=ctr_scene_texture_registers[unit];
        const uint32_t values[5]={UINT32_C(0x00080008),(filters&(1u<<unit))?6u:0u,0u,
            material_addresses[unit]/8u,0u};
        for(unsigned field=0;field<5u;++field,at+=8u)insert(fixture,at,registers[field],values[field]);
    }
    unsigned stage=0;
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        unsigned source=stage?15u|((unit+3u)<<4):unit+3u;
        set(fixture,ctr_tev_registers[stage],source|(source<<16));
        set(fixture,ctr_tev_registers[stage]+2u,stage?UINT32_C(0x00010001):0u);++stage;
    }
    for(;stage<6u;++stage) {
        set(fixture,ctr_tev_registers[stage],stage?UINT32_C(0x000f000f):0u);
        set(fixture,ctr_tev_registers[stage]+2u,0u);
    }
    finish(fixture);
}
static void material_state(ctr_scene_state *state) {
    target_state(state);
    const float uv[2][2]={{0.375f,0.625f},{0.875f,0.125f}};
    for(unsigned uniform=0;uniform<2u;++uniform) {
        state->shader.uniform_defined[uniform]=15u;
        for(unsigned lane=0;lane<4u;++lane)
            state->shader.uniforms[uniform][lane]=bits(lane<2u?uv[uniform][lane]:0.0f);
    }
}
static void material_expected(unsigned mask,bool alternate,unsigned filters,uint8_t rgba[4]) {
    /* At 8x8, each chosen UV lies on integer texel boundaries. Nearest reads
     * (2,6)/(3,5)/(7,1); bilinear averages the four neighboring centers once.
     * Raw V rows are inverted independently of the byte-pattern producer. */
    const unsigned points[3][2]={{2u,6u},{3u,5u},{7u,1u}};
    bool first=true;
    for(unsigned unit=0;unit<3u;++unit)if(mask&(1u<<unit)) {
        unsigned coordinate=unit==2u && alternate?1u:unit;
        unsigned x=points[coordinate][0],v=points[coordinate][1];
        for(unsigned lane=0;lane<4u;++lane) {
            unsigned sample;
            if(filters&(1u<<unit)) {
                unsigned sum=material_texel(unit,x-1u,8u-v,lane)+material_texel(unit,x,8u-v,lane)+
                    material_texel(unit,x-1u,7u-v,lane)+material_texel(unit,x,7u-v,lane);
                sample=(sum+2u)/4u;
            } else sample=material_texel(unit,x,7u-v,lane);
            rgba[lane]=(uint8_t)(first?sample:(unsigned)rgba[lane]*sample/255u);
        }
        first=false;
    }
}
static void material_pixels(const uint8_t *pixels,const uint8_t expected[4]) {
    const uint8_t background[4]={16,32,48,255};
    for(unsigned y=0;y<64u;++y)for(unsigned x=0;x<64u;++x)
        CHECK(!memcmp(pixels+(y*64u+x)*4u,x>=8u && x<56u && y>=8u && y<56u?expected:background,4u));
}
static void multiunit_images(void) {
    uint8_t raw[3][256],pixels[CTR_SCENE_FIXTURE_BYTES];ctr_scene_resource_span spans[3];
    material_sources(raw,spans);ctr_scene_fixture fixture;
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,0,spans,3};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};ctr_scene_report report;
    for(unsigned mask=1;mask<8u;++mask)for(unsigned alternate=0;alternate<2u;++alternate)
        for(unsigned filters=0;filters<8u;++filters) {
            unsigned coordinates=(mask&3u)|((mask&4u)?alternate?2u:4u:0u);
            material_fixture(&fixture,mask,coordinates,alternate!=0u,filters);resources.size=fixture.resource_bytes;
            ctr_scene_state state;material_state(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
            CHECK(ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
                  UINT32_C(0x18010000),&report)==CTR_SCENE_OK);
            CHECK(report.draws==2 && report.triangles==2 && report.fragments==2304);
            uint8_t expected[4];material_expected(mask,alternate!=0u,filters,expected);material_pixels(pixels,expected);
        }
    /* Every nonzero unit uses the same supported decoder formats, including
     * compressed formats and their format bit, rather than a unit0 shortcut. */
    const unsigned bpp[14]={32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    const unsigned alpha[14]={0,255,0,255,0,0,255,255,0,0,255,0,255,0};
    uint8_t blank[256]={0};
    for(unsigned unit=1;unit<3u;++unit)for(unsigned format=0;format<14u;++format) {
        material_fixture(&fixture,1u<<unit,1u<<unit,false,0);resources.size=fixture.resource_bytes;
        set(&fixture,ctr_scene_texture_registers[unit][CTR_SCENE_TEXTURE_FORMAT],format);
        set(&fixture,ctr_scene_texture_registers[unit][CTR_SCENE_TEXTURE_PARAMETERS],format>=12u?32u:0u);
        ctr_scene_resource_span span={material_addresses[unit],blank,8u*8u*bpp[format]/8u};
        resources.textures=&span;resources.texture_count=1;
        ctr_scene_state state;material_state(&state);ctr_scene_fixture_background(pixels,sizeof(pixels));
        CHECK(ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&report)==CTR_SCENE_OK);
        const uint8_t expected[4]={(uint8_t)(format>=12u?2u:0u),(uint8_t)(format>=12u?2u:0u),
            (uint8_t)(format>=12u?2u:0u),(uint8_t)alpha[format]};
        material_pixels(pixels,expected);
    }
}
typedef struct {
    const uint8_t *expected;
    unsigned mask,begins,draws,ends,aborts,failure;
} material_backend;
static bool material_begin(void *context,const ctr_scene_surface *initial) {
    material_backend *b=context;++b->begins;CHECK(initial->width==64u && initial->height==64u);
    return b->failure!=1u;
}
static bool material_draw(void *context,const ctr_scene_state *state,const ctr_vertex *vertices,unsigned count,
                          const ctr_scene_texture textures[3],unsigned mask) {
    material_backend *b=context;++b->draws;
    CHECK(mask==b->mask && count==3u && vertices[0].enabled==7u && (state->registers[0x080]&7u)==mask);
    for(unsigned unit=0;unit<3u;++unit) {
        const ctr_scene_texture *t=&textures[unit];
        if(mask&(1u<<unit)) {
            CHECK(t->rgba && t->width==8u && t->height==8u && t->address==material_addresses[unit] && !t->format);
            for(unsigned lane=0;lane<4u;++lane)CHECK(t->rgba[lane]==material_texel(unit,0,0,lane));
        } else CHECK(!t->rgba && !t->width && !t->height && !t->address && !t->format && !t->params);
    }
    return b->failure!=2u;
}
static bool material_end(void *context,const ctr_scene_surface *readback) {
    material_backend *b=context;++b->ends;memcpy(readback->bytes,b->expected,readback->size);
    if(b->failure==4u)((uint8_t *)readback->bytes)[readback->size-1u]^=1u;
    return b->failure!=3u;
}
static void material_abort(void *context) { material_backend *b=context;++b->aborts; }
static void multiunit_rollback_and_backend(void) {
    ctr_scene_fixture fixture;uint8_t raw[3][256],pixels[CTR_SCENE_FIXTURE_BYTES],before[CTR_SCENE_FIXTURE_BYTES],reference[CTR_SCENE_FIXTURE_BYTES];
    ctr_scene_resource_span spans[3];material_sources(raw,spans);
    ctr_scene_resources resources={UINT32_C(0x18000000),fixture.resources,0,spans,3};
    ctr_scene_surface surface={pixels,sizeof(pixels),64,64,256};ctr_scene_report report;
    ctr_scene_fixture_background(before,sizeof(before));
    material_fixture(&fixture,7u,7u,false,2u);resources.size=fixture.resource_bytes;
    ctr_scene_state state,previous,expected;material_state(&state);memcpy(pixels,before,sizeof(pixels));
    CHECK(ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&report)==CTR_SCENE_OK);memcpy(reference,pixels,sizeof(reference));expected=state;
    for(unsigned failure=0;failure<5u;++failure) {
        material_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
        material_backend context={reference,7u,0,0,0,0,failure};
        ctr_scene_backend backend={&context,material_begin,NULL,material_end,material_abort,NULL,NULL,material_draw};
        CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)==(failure?CTR_SCENE_BACKEND:CTR_SCENE_OK));
        CHECK(context.begins==1u && context.draws==(failure==1u?0u:failure==2u?1u:2u) &&
            context.ends==(failure==1u || failure==2u?0u:1u) && context.aborts==(failure?1u:0u));
        CHECK(!memcmp(&state,failure?&previous:&expected,sizeof(state)) &&
              !memcmp(pixels,failure?before:reference,sizeof(pixels)));
    }
    /* Only-multi owners can render the legacy0/1 routes; disabled entries have
     * no borrowed image. Legacy-only owners reject actual multi draws before
     * begin even when the final register state disables the extra units. */
    material_fixture(&fixture,1u,1u,false,0);resources.size=fixture.resource_bytes;
    material_state(&state);memcpy(pixels,before,sizeof(pixels));
    CHECK(ctr_scene_execute_target(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&report)==CTR_SCENE_OK);memcpy(reference,pixels,sizeof(reference));
    material_state(&state);memcpy(pixels,before,sizeof(pixels));
    material_backend context={reference,1u,0,0,0,0,0};
    ctr_scene_backend backend={&context,material_begin,NULL,material_end,material_abort,NULL,NULL,material_draw};
    CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&backend,&report)==CTR_SCENE_OK);
    CHECK(context.begins==1u && context.draws==2u && context.ends==1u && !context.aborts);
    material_fixture(&fixture,7u,7u,false,0);append(&fixture,0x080,1u);resources.size=fixture.resource_bytes;
    material_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
    texture_backend legacy={reference,0,0,0,0,false};
    ctr_scene_backend old={&legacy,texture_begin,texture_draw,texture_end,texture_abort,NULL,NULL,NULL};
    CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
          UINT32_C(0x18010000),&old,&report)==CTR_SCENE_UNSUPPORTED);
    CHECK(!legacy.begins && !legacy.draws && !legacy.ends && !legacy.aborts && !report.draws);
    CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
    for(unsigned failure=0;failure<11u;++failure) {
        material_fixture(&fixture,7u,7u,false,0);resources.size=fixture.resource_bytes;
        material_state(&state);previous=state;memcpy(pixels,before,sizeof(pixels));
        if(failure==0u)append(&fixture,0x093,2u);
        if(failure==1u)append(&fixture,0x09c,1u);
        if(failure==2u)append(&fixture,0x096,14u);
        if(failure==3u)append(&fixture,0x080,8u);
        if(failure==4u)set(&fixture,0x095,(material_addresses[1]+248u)/8u);
        if(failure==5u)set(&fixture,0x053,UINT32_C(0x1f1f1f0e));
        if(failure==6u) { set(&fixture,0x053,UINT32_C(0x1f1f1f1f));set(&fixture,0x06f,0x503u); }
        if(failure==7u) { size_t at=find(&fixture,0x09a,0)+4u;put32(fixture.commands+at,(get32(fixture.commands+at)&~UINT32_C(0xf0000))|UINT32_C(0xe0000)); }
        if(failure==8u)set(&fixture,0x09d,UINT32_C(0x10000000));
        if(failure==9u) { set(&fixture,0x080,3u); /* Active TEV Texture2 is disabled. */ }
        if(failure==10u) { set(&fixture,0x06f,0x303u); /* UV2 export clock is absent. */ }
        context=(material_backend){reference,7u,0,0,0,0,0};backend.context=&context;
        CHECK(ctr_scene_execute_target_backend(&state,fixture.commands,fixture.command_bytes,&resources,&surface,
              UINT32_C(0x18010000),&backend,&report)!=CTR_SCENE_OK);
        CHECK(!context.begins && !context.draws && !context.ends && !context.aborts);
        CHECK(!memcmp(&state,&previous,sizeof(state)) && !memcmp(pixels,before,sizeof(pixels)));
        if(failure<4u)CHECK(report.draws==2u);
    }
}

int main(void) {
    floats24();shader_patterns();shader_arithmetic();arithmetic_boundaries();shader_errors();shader_capacity();
    scenes();arithmetic_scenes();scene_capacity();scene_errors();backend_full_target();textured_scenes();textured_rollback();
    fragment_contract();fragment_scenes();blend_contract();blend_scenes();
    depth_comparisons();depth_overdraw();depth_quantization();depth_rollback();depth_backend_parity();
    stencil_contract();stencil_images();stencil_depth_comparisons();stencil_order_and_retention();stencil_quantization();stencil_rollback();stencil_backend_parity();stencil_capacity();
    buffer_images();buffer_retention();buffer_rollback();buffer_backend_parity();
    multiunit_images();multiunit_rollback_and_backend();
    printf("original shader/scene tests: %u assertions passed\n",assertions);return 0;
}
