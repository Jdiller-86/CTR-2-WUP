#include "ctr_vertex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions;if(!(x)) { fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1); } } while(0)
static void put32(uint8_t *bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t float_bits(float value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));return bits;
}
static void defaults(const ctr_vertex *vertex,unsigned slot,unsigned components) {
    CHECK(vertex->enabled==(uint16_t)(1u<<slot));
    for(unsigned input=0;input<CTR_VERTEX_INPUTS;++input)
        for(unsigned lane=0;lane<4;++lane)
            if(input!=slot || lane>=components)
                CHECK(vertex->input[input][lane]==(lane==3?1.0f:0.0f));
}
static void formats(void) {
    uint8_t bytes[4096*2+1];ctr_vertex vertices[4096];
    ctr_vertex_attribute attribute={0,7,CTR_VERTEX_BYTE,1,0};
    ctr_vertex_stream stream={bytes+1,256,0,1};
    for(unsigned i=0;i<256;++i)bytes[i+1]=(uint8_t)i;
    for(unsigned format=0;format<2;++format) {
        attribute.format=format;
        CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,256,vertices,4096)==CTR_VERTEX_OK);
        for(unsigned i=0;i<256;++i) {
            int expected=(format==CTR_VERTEX_BYTE && i>=128)?(int)i-256:(int)i;
            CHECK(vertices[i].input[7][0]==(float)expected);
            defaults(&vertices[i],7,1);
        }
    }
    attribute.format=CTR_VERTEX_SHORT;stream.stride=2;stream.size=8192;
    /* Every signed short, in original little-endian bytes and an unaligned
     * backing view, across sixteen bounded decode calls. */
    for(unsigned base=0;base<65536;base+=4096) {
        for(unsigned i=0;i<4096;++i) {
            unsigned value=base+i;bytes[1+i*2]=(uint8_t)value;bytes[2+i*2]=(uint8_t)(value>>8);
        }
        CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,4096,vertices,4096)==CTR_VERTEX_OK);
        for(unsigned i=0;i<4096;++i) {
            int value=(int)(base+i);if(value>=32768)value-=65536;
            CHECK(vertices[i].input[7][0]==(float)value);
            CHECK(vertices[i].enabled==128 && vertices[i].input[7][1]==0.0f &&
                  vertices[i].input[7][2]==0.0f && vertices[i].input[7][3]==1.0f);
        }
    }
    static const uint32_t patterns[]={0,UINT32_C(0x80000000),UINT32_C(0x3f800000),
        UINT32_C(0xc0200000),UINT32_C(0x7f7fffff),UINT32_C(0xff7fffff),
        UINT32_C(0x00800000),UINT32_C(0x80800000),1,UINT32_C(0x80000001),
        UINT32_C(0x3f800001),UINT32_C(0x3eaaaaab)};
    attribute.format=CTR_VERTEX_FLOAT;attribute.input=15;stream.stride=4;stream.size=sizeof(patterns);
    for(unsigned i=0;i<sizeof(patterns)/sizeof(patterns[0]);++i)put32(bytes+1+i*4,patterns[i]);
    CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,12,vertices,4096)==CTR_VERTEX_OK);
    for(unsigned i=0;i<12;++i) {
        CHECK(float_bits(vertices[i].input[15][0])==patterns[i]);defaults(&vertices[i],15,1);
    }
    /* All sixteen GPU_ATTRIBFMT nibbles: two format bits followed by size-1.
     * Lanes are supplied without normalization, including nonzero W. */
    for(unsigned descriptor=0;descriptor<16;++descriptor) {
        unsigned format=descriptor&3u,count=(descriptor>>2)+1u;
        unsigned width=format==3?4u:format==2?2u:1u;
        attribute.format=format;attribute.components=count;stream.stride=16;stream.size=16;
        memset(bytes+1,0,16);
        for(unsigned lane=0;lane<count;++lane) {
            if(format==CTR_VERTEX_FLOAT)put32(bytes+1+lane*4,UINT32_C(0x3f800000)+lane*UINT32_C(0x00800000));
            else bytes[1+lane*width]=(uint8_t)(lane+1);
        }
        CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,1,vertices,1)==CTR_VERTEX_OK);
        for(unsigned lane=0;lane<count;++lane)
            CHECK(vertices[0].input[15][lane]==(format==CTR_VERTEX_FLOAT?(float)(1u<<lane):(float)(lane+1)));
        defaults(&vertices[0],15,count);
    }
}
static void layout(void) {
    uint8_t bytes[4*255+1];memset(bytes,0xcd,sizeof(bytes));
    ctr_vertex_stream stream={bytes+1,4*255,3,255};
    ctr_vertex_attribute attributes[12];ctr_vertex vertices[3],reordered[3];
    for(unsigned a=0;a<12;++a)attributes[a]=(ctr_vertex_attribute){0,a+4,CTR_VERTEX_UNSIGNED_BYTE,4,a*4};
    for(unsigned vertex=0;vertex<4;++vertex)
        for(unsigned a=0;a<12;++a)
            for(unsigned lane=0;lane<4;++lane)bytes[1+3+vertex*255+a*4+lane]=(uint8_t)(vertex*40+a*3+lane);
    CHECK(ctr_vertex_decode(attributes,12,&stream,1,1,3,vertices,3)==CTR_VERTEX_OK);
    for(unsigned vertex=0;vertex<3;++vertex) {
        CHECK(vertices[vertex].enabled==UINT16_C(0xfff0));
        for(unsigned a=0;a<12;++a)
            for(unsigned lane=0;lane<4;++lane)
                CHECK(vertices[vertex].input[a+4][lane]==(float)((vertex+1)*40+a*3+lane));
        for(unsigned input=0;input<4;++input)
            CHECK(vertices[vertex].input[input][0]==0.0f && vertices[vertex].input[input][3]==1.0f);
    }
    for(unsigned a=0;a<6;++a) {
        ctr_vertex_attribute previous=attributes[a];attributes[a]=attributes[11-a];attributes[11-a]=previous;
    }
    CHECK(ctr_vertex_decode(attributes,12,&stream,1,1,3,reordered,3)==CTR_VERTEX_OK);
    for(unsigned vertex=0;vertex<3;++vertex) {
        CHECK(vertices[vertex].enabled==reordered[vertex].enabled);
        CHECK(!memcmp(vertices[vertex].input,reordered[vertex].input,sizeof(vertices[vertex].input)));
    }
    /* Separate streams, arbitrary permutation, gaps and unaligned offsets. */
    ctr_vertex_stream streams[12];uint8_t storage[12][21];
    for(unsigned a=0;a<12;++a) {
        memset(storage[a],0xee,sizeof(storage[a]));
        streams[a]=(ctr_vertex_stream){storage[a]+1,20,1,9};
        attributes[a]=(ctr_vertex_attribute){a,(a*5)%16,CTR_VERTEX_SHORT,2,1};
        for(unsigned vertex=0;vertex<2;++vertex) {
            int value=(int)a-(int)vertex*100;
            storage[a][3+vertex*9]=(uint8_t)value;storage[a][4+vertex*9]=(uint8_t)((unsigned)value>>8);
            storage[a][5+vertex*9]=0xff;storage[a][6+vertex*9]=0x7f;
        }
    }
    CHECK(ctr_vertex_decode(attributes,12,streams,12,0,2,vertices,3)==CTR_VERTEX_OK);
    for(unsigned vertex=0;vertex<2;++vertex)
        for(unsigned a=0;a<12;++a) {
            unsigned slot=(a*5)%16;
            CHECK(vertices[vertex].input[slot][0]==(float)((int)a-(int)vertex*100));
            CHECK(vertices[vertex].input[slot][1]==32767.0f && vertices[vertex].input[slot][2]==0.0f && vertices[vertex].input[slot][3]==1.0f);
        }
    /* Validation-only obeys the same layout and byte contract. */
    CHECK(ctr_vertex_decode(attributes,12,streams,12,0,2,NULL,0)==CTR_VERTEX_OK);
    uint8_t order[]={1,0,1};ctr_vertex_indices indices={order,sizeof(order),0,0};
    CHECK(ctr_vertex_decode_indexed(attributes,12,streams,12,&indices,0,3,vertices,3)==CTR_VERTEX_OK);
    for(unsigned vertex=0;vertex<3;++vertex)
        for(unsigned a=0;a<12;++a) {
            unsigned slot=(a*5)%16;
            CHECK(vertices[vertex].input[slot][0]==(float)((int)a-(int)order[vertex]*100));
            CHECK(vertices[vertex].input[slot][1]==32767.0f && vertices[vertex].input[slot][3]==1.0f);
        }
}
static void interleaved(void) {
    /* Original triangle-like records: position, raw color bytes, signed texture
     * integers and padding. This only decodes records; it submits no triangle. */
    uint8_t bytes[128];memset(bytes,0xde,sizeof(bytes));
    ctr_vertex_attribute attributes[]={
        {0,0,CTR_VERTEX_FLOAT,3,0},{0,3,CTR_VERTEX_UNSIGNED_BYTE,4,12},
        {0,9,CTR_VERTEX_SHORT,2,18}};
    ctr_vertex_stream stream={bytes+1,127,5,24};ctr_vertex output[4];
    memset(output,0xa5,sizeof(output));uint8_t untouched[sizeof(ctr_vertex)];
    memcpy(untouched,&output[3],sizeof(untouched));
    const uint32_t positions[3][3]={{UINT32_C(0xbf800000),UINT32_C(0xbf800000),0},
        {UINT32_C(0x3f800000),UINT32_C(0xbf800000),UINT32_C(0x3f000000)},
        {0,UINT32_C(0x3f800000),UINT32_C(0x80000000)}};
    const uint8_t colors[3][4]={{255,0,0,255},{0,128,0,255},{0,0,255,64}};
    const int coordinates[3][2]={{0,0},{32767,-32768},{1234,-5678}};
    for(unsigned vertex=0;vertex<3;++vertex) {
        uint8_t *record=bytes+6+vertex*24;
        for(unsigned lane=0;lane<3;++lane)put32(record+lane*4,positions[vertex][lane]);
        memcpy(record+12,colors[vertex],4);
        for(unsigned lane=0;lane<2;++lane) {
            unsigned bits=(unsigned)coordinates[vertex][lane];
            record[18+lane*2]=(uint8_t)bits;record[19+lane*2]=(uint8_t)(bits>>8);
        }
    }
    uint8_t previous[sizeof(bytes)];memcpy(previous,bytes,sizeof(bytes));
    CHECK(ctr_vertex_decode(attributes,3,&stream,1,0,3,output,4)==CTR_VERTEX_OK);
    CHECK(!memcmp(bytes,previous,sizeof(bytes)));
    CHECK(!memcmp(&output[3],untouched,sizeof(untouched)));
    for(unsigned vertex=0;vertex<3;++vertex) {
        CHECK(output[vertex].enabled==((1u<<0)|(1u<<3)|(1u<<9)));
        for(unsigned lane=0;lane<3;++lane)CHECK(float_bits(output[vertex].input[0][lane])==positions[vertex][lane]);
        for(unsigned lane=0;lane<4;++lane)CHECK(output[vertex].input[3][lane]==(float)colors[vertex][lane]);
        for(unsigned lane=0;lane<2;++lane)CHECK(output[vertex].input[9][lane]==(float)coordinates[vertex][lane]);
        CHECK(output[vertex].input[0][3]==1.0f && output[vertex].input[9][2]==0.0f && output[vertex].input[9][3]==1.0f);
    }
    /* Skip a prefix in an unaligned little-endian u16 view. The index order
     * repeats a vertex and does not add a nonindexed first-vertex offset. */
    uint8_t order_bytes[]={0xee,0xff,0xff,0xff,0xff,2,0,0,0,2,0,1,0};
    ctr_vertex_indices indices={order_bytes,sizeof(order_bytes),1,CTR_VERTEX_INDEX_UNSIGNED_SHORT};
    const unsigned order[]={2,0,2,1};
    CHECK(ctr_vertex_decode_indexed(attributes,3,&stream,1,&indices,2,4,output,4)==CTR_VERTEX_OK);
    CHECK(!memcmp(bytes,previous,sizeof(bytes)));
    for(unsigned vertex=0;vertex<4;++vertex) {
        unsigned id=order[vertex];
        for(unsigned lane=0;lane<3;++lane)CHECK(float_bits(output[vertex].input[0][lane])==positions[id][lane]);
        for(unsigned lane=0;lane<4;++lane)CHECK(output[vertex].input[3][lane]==(float)colors[id][lane]);
        for(unsigned lane=0;lane<2;++lane)CHECK(output[vertex].input[9][lane]==(float)coordinates[id][lane]);
        CHECK(output[vertex].enabled==((1u<<0)|(1u<<3)|(1u<<9)));
    }
}
static void indexed_formats(void) {
    static uint8_t source[65536*2+1],encoded[4096*2+2];
    static ctr_vertex output[4097];
    ctr_vertex_attribute attribute={0,5,CTR_VERTEX_UNSIGNED_BYTE,2,0};
    ctr_vertex_stream stream={source+1,65536*2,0,2};
    for(unsigned id=0;id<65536;++id) {
        source[1+id*2]=(uint8_t)id;source[2+id*2]=(uint8_t)(id>>8);
    }
    memset(output,0xa5,sizeof(output));uint8_t untouched[sizeof(ctr_vertex)];
    memcpy(untouched,&output[4096],sizeof(untouched));
    for(unsigned format=0;format<2;++format) {
        unsigned width=format+1,count=format?4096u:256u,limit=format?65536u:256u;
        ctr_vertex_indices indices={encoded+1,count*width+1u,1,format};
        for(unsigned base=0;base<limit;base+=count) {
            for(unsigned i=0;i<count;++i) {
                unsigned id=base+count-1-i;
                encoded[2+i*width]=(uint8_t)id;
                if(width==2)encoded[3+i*width]=(uint8_t)(id>>8);
            }
            CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,count,output,4097)==CTR_VERTEX_OK);
            for(unsigned i=0;i<count;++i) {
                unsigned id=base+count-1-i;
                CHECK(output[i].input[5][0]==(float)(id&255u));
                CHECK(output[i].input[5][1]==(float)(id>>8));
                CHECK(output[i].enabled==32 && output[i].input[5][2]==0.0f && output[i].input[5][3]==1.0f);
            }
            CHECK(!memcmp(&output[4096],untouched,sizeof(untouched)));
        }
    }
}
static void indexed_failures(void) {
    uint8_t bytes[12],encoded[8]={0,2};
    put32(bytes,UINT32_C(0x3f800000));put32(bytes+4,UINT32_C(0x7fc00000));put32(bytes+8,UINT32_C(0x80000000));
    ctr_vertex_attribute attribute={0,0,CTR_VERTEX_FLOAT,1,0};
    ctr_vertex_stream stream={bytes,sizeof(bytes),0,4};
    ctr_vertex_indices indices={encoded,sizeof(encoded),0,CTR_VERTEX_INDEX_UNSIGNED_BYTE};
    ctr_vertex output[2],previous[2];memset(output,0xa5,sizeof(output));memcpy(previous,output,sizeof(output));
#define FAIL(expression,result) do { CHECK((expression)==(result));CHECK(!memcmp(output,previous,sizeof(output))); } while(0)
#define DECODE(first,count,capacity) ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,first,count,output,capacity)
    FAIL(ctr_vertex_decode_indexed(&attribute,1,&stream,1,NULL,0,1,output,2),CTR_VERTEX_ARGUMENT);
    FAIL(ctr_vertex_decode_indexed(&attribute,1,&stream,1,(const ctr_vertex_indices *)(UINTPTR_MAX-2),0,1,output,2),CTR_VERTEX_OVERFLOW);
    FAIL(DECODE(0,4097,2),CTR_VERTEX_LIMIT);
    FAIL(DECODE(0,2,1),CTR_VERTEX_CAPACITY);
    indices.format=2;FAIL(DECODE(0,1,2),CTR_VERTEX_UNSUPPORTED);
    indices.format=0;indices.offset=9;FAIL(DECODE(0,1,2),CTR_VERTEX_RANGE);
    indices.offset=0;indices.bytes=NULL;FAIL(DECODE(0,1,2),CTR_VERTEX_ARGUMENT);
    indices.size=0;FAIL(DECODE(0,1,2),CTR_VERTEX_RANGE);
    CHECK(DECODE(SIZE_MAX,0,0)==CTR_VERTEX_OK);CHECK(!memcmp(output,previous,sizeof(output)));
    indices.bytes=encoded;indices.size=CTR_VERTEX_MAX_SOURCE_BYTES+1u;
    FAIL(DECODE(0,0,0),CTR_VERTEX_LIMIT);
    indices.bytes=(const void *)(UINTPTR_MAX-3);indices.size=8;
    FAIL(DECODE(0,1,2),CTR_VERTEX_OVERFLOW);
    indices.bytes=encoded;FAIL(DECODE(SIZE_MAX,2,2),CTR_VERTEX_OVERFLOW);
    indices.format=CTR_VERTEX_INDEX_UNSIGNED_SHORT;
    FAIL(DECODE(SIZE_MAX/2+1,1,2),CTR_VERTEX_OVERFLOW);
    indices.offset=2;FAIL(DECODE(SIZE_MAX/2,1,2),CTR_VERTEX_OVERFLOW);
    indices.offset=0;indices.size=3;FAIL(DECODE(0,2,2),CTR_VERTEX_RANGE);
    indices.format=0;indices.size=1;FAIL(DECODE(0,2,2),CTR_VERTEX_RANGE);
    indices.size=8;encoded[1]=3;FAIL(DECODE(0,2,2),CTR_VERTEX_RANGE);
    encoded[1]=1;FAIL(DECODE(0,2,2),CTR_VERTEX_UNSUPPORTED);
    FAIL(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,2,NULL,0),CTR_VERTEX_UNSUPPORTED);
    /* The NaN in the unreferenced middle vertex must not reject this gather. */
    encoded[1]=2;
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,2,NULL,0)==CTR_VERTEX_OK);
    CHECK(DECODE(0,2,2)==CTR_VERTEX_OK);
    CHECK(output[0].input[0][0]==1.0f && float_bits(output[1].input[0][0])==UINT32_C(0x80000000));
#undef DECODE
#undef FAIL
}
static void indexed_aliases(void) {
    union { ctr_vertex output[2];ctr_vertex_indices indices;uint8_t bytes[1024]; } storage;
    uint8_t before[sizeof(storage)],source[4]={1,0,2,3},encoded[]={0};
    ctr_vertex_attribute attribute={0,0,CTR_VERTEX_UNSIGNED_BYTE,1,0};
    ctr_vertex_stream stream={source,sizeof(source),0,1};
    ctr_vertex_indices indices={encoded,sizeof(encoded),0,0};
    memset(&storage,0xa5,sizeof(storage));storage.indices=indices;memcpy(before,&storage,sizeof(storage));
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&storage.indices,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    memset(&storage,0xa5,sizeof(storage));memcpy(before,&storage,sizeof(storage));
    indices=(ctr_vertex_indices){storage.bytes,sizeof(storage.bytes),800,0};
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    indices=(ctr_vertex_indices){storage.bytes+sizeof(ctr_vertex)-1,2,1,0};
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    /* Adjacent output/index spans and shared read-only index/attribute bytes. */
    storage.bytes[sizeof(ctr_vertex)]=0;
    indices=(ctr_vertex_indices){storage.bytes+sizeof(ctr_vertex),1,0,0};
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,1,storage.output,1)==CTR_VERTEX_OK);
    CHECK(storage.output[0].input[0][0]==1.0f && storage.bytes[sizeof(ctr_vertex)]==0);
    indices=(ctr_vertex_indices){source,sizeof(source),0,0};
    CHECK(ctr_vertex_decode_indexed(&attribute,1,&stream,1,&indices,0,1,storage.output,1)==CTR_VERTEX_OK);
    CHECK(storage.output[0].input[0][0]==0.0f);
}
static void failures(void) {
    uint8_t bytes[32]={1,2,3,4};ctr_vertex_attribute attributes[2]={{0,0,0,1,0},{0,1,0,1,1}};
    ctr_vertex_stream streams[2]={{bytes,sizeof(bytes),0,4},{bytes,sizeof(bytes),0,4}};
    ctr_vertex output[2],previous[2];memset(output,0xa5,sizeof(output));memcpy(previous,output,sizeof(output));
#define FAIL(expression,result) do { CHECK((expression)==(result));CHECK(!memcmp(output,previous,sizeof(output))); } while(0)
    FAIL(ctr_vertex_decode(NULL,1,streams,1,0,1,output,2),CTR_VERTEX_ARGUMENT);
    FAIL(ctr_vertex_decode(attributes,1,NULL,1,0,1,output,2),CTR_VERTEX_ARGUMENT);
    FAIL(ctr_vertex_decode(attributes,0,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    FAIL(ctr_vertex_decode(attributes,1,streams,0,0,1,output,2),CTR_VERTEX_LAYOUT);
    FAIL(ctr_vertex_decode(attributes,13,streams,1,0,1,output,2),CTR_VERTEX_LIMIT);
    FAIL(ctr_vertex_decode(attributes,1,streams,13,0,1,output,2),CTR_VERTEX_LIMIT);
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,4097,output,2),CTR_VERTEX_LIMIT);
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,2,output,1),CTR_VERTEX_CAPACITY);
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,NULL,1),CTR_VERTEX_ARGUMENT);
    attributes[1].input=0;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].input=1;attributes[1].format=4;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_UNSUPPORTED);
    attributes[1].format=0;attributes[1].input=16;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].input=1;attributes[1].stream=1;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].stream=0;attributes[1].components=0;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].components=5;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].components=1;attributes[1].offset=4;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    attributes[1].offset=1;streams[0].stride=0;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    streams[0].stride=256;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_LAYOUT);
    streams[0].stride=4;streams[0].offset=33;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_RANGE);
    streams[0].offset=0;streams[0].size=4;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,2,output,2),CTR_VERTEX_RANGE);
    streams[0].size=5;attributes[1].offset=1;
    FAIL(ctr_vertex_decode(attributes,2,streams,1,0,2,output,2),CTR_VERTEX_RANGE);
    streams[0].size=sizeof(bytes);
    FAIL(ctr_vertex_decode(attributes,1,streams,1,SIZE_MAX,2,output,2),CTR_VERTEX_OVERFLOW);
    FAIL(ctr_vertex_decode(attributes,1,streams,1,SIZE_MAX/4+1,1,output,2),CTR_VERTEX_OVERFLOW);
    /* Address arithmetic and pointer-span wrap are rejected before loads. */
    streams[0].offset=4;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,SIZE_MAX/4,1,output,2),CTR_VERTEX_OVERFLOW);
    streams[0].offset=0;streams[0].bytes=(const void *)(UINTPTR_MAX-3);streams[0].size=8;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_OVERFLOW);
    streams[0].bytes=NULL;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_ARGUMENT);
    streams[0].size=0;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_RANGE);
    CHECK(ctr_vertex_decode(attributes,1,streams,1,SIZE_MAX,0,output,0)==CTR_VERTEX_OK);
    CHECK(!memcmp(output,previous,sizeof(output)));
    streams[0].bytes=bytes;streams[0].size=sizeof(bytes);
    attributes[0].format=CTR_VERTEX_FLOAT;attributes[0].components=1;
    static const uint32_t rejected[]={UINT32_C(0x7f800000),UINT32_C(0xff800000),UINT32_C(0x7fc00000),UINT32_C(0x7f800001)};
    for(unsigned i=0;i<4;++i) {
        put32(bytes,UINT32_C(0x3f800000));put32(bytes+4,rejected[i]);
        FAIL(ctr_vertex_decode(attributes,1,streams,1,0,2,output,2),CTR_VERTEX_UNSUPPORTED);
        FAIL(ctr_vertex_decode(attributes,1,streams,1,0,2,NULL,0),CTR_VERTEX_UNSUPPORTED);
    }
    uint8_t *oversized=malloc(CTR_VERTEX_MAX_SOURCE_BYTES+1u);CHECK(oversized);
    streams[0].bytes=oversized;streams[0].size=CTR_VERTEX_MAX_SOURCE_BYTES+1u;
    FAIL(ctr_vertex_decode(attributes,1,streams,1,0,1,output,2),CTR_VERTEX_LIMIT);
    free(oversized);
#undef FAIL
}
static void aliases(void) {
    union { ctr_vertex output[2];ctr_vertex_attribute attributes[2];ctr_vertex_stream streams[2];uint8_t bytes[1024]; } storage;
    uint8_t before[sizeof(storage)],source[32]={1};
    ctr_vertex_attribute attribute={0,0,CTR_VERTEX_UNSIGNED_BYTE,1,0};
    ctr_vertex_stream stream={source,sizeof(source),0,1};
    memset(&storage,0xa5,sizeof(storage));storage.attributes[0]=attribute;memcpy(before,&storage,sizeof(storage));
    CHECK(ctr_vertex_decode(storage.attributes,1,&stream,1,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    memset(&storage,0xa5,sizeof(storage));storage.streams[0]=stream;memcpy(before,&storage,sizeof(storage));
    CHECK(ctr_vertex_decode(&attribute,1,storage.streams,1,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    memset(&storage,0xa5,sizeof(storage));memcpy(before,&storage,sizeof(storage));
    stream.bytes=storage.bytes;stream.size=sizeof(storage.bytes);
    CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    /* Alias an input's unused tail: entire supplied spans are protected. */
    stream.bytes=storage.bytes+sizeof(ctr_vertex)-4;stream.size=8;
    CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,1,storage.output,2)==CTR_VERTEX_ALIAS);
    CHECK(!memcmp(before,&storage,sizeof(storage)));
    /* Adjacent spans do not overlap, and read-only streams may share bytes. */
    stream.bytes=storage.bytes+sizeof(ctr_vertex);stream.size=1;
    CHECK(ctr_vertex_decode(&attribute,1,&stream,1,0,1,storage.output,1)==CTR_VERTEX_OK);
    CHECK(storage.output[0].input[0][0]==165.0f && storage.bytes[sizeof(ctr_vertex)]==0xa5);
    CHECK(!strcmp(ctr_vertex_result_name(CTR_VERTEX_UNSUPPORTED),"unsupported"));
    CHECK(!strcmp(ctr_vertex_result_name((ctr_vertex_result)99),"unknown"));
}
int main(void) {
    formats();layout();interleaved();indexed_formats();indexed_failures();indexed_aliases();failures();aliases();
    printf("vertex attribute tests: %u assertions passed\n",assertions);return 0;
}
