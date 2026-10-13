/* Original decoder of published PICA attribute format facts. No emulator linked. */
#include "ctr_vertex.h"
#include <float.h>
#include <stdbool.h>
#include <string.h>

static bool span(const void *pointer,size_t size) {
    return (!size || pointer) && size<=UINTPTR_MAX-(uintptr_t)pointer;
}
static bool overlaps(const void *a,size_t as,const void *b,size_t bs) {
    return as && bs && (uintptr_t)a<(uintptr_t)b+bs && (uintptr_t)b<(uintptr_t)a+as;
}
static uint32_t little32(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|
           (uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}
static float component(const uint8_t *bytes,unsigned format) {
    if(format==CTR_VERTEX_FLOAT) {
        uint32_t bits=little32(bytes);float value;
        memcpy(&value,&bits,sizeof(value));return value;
    }
    int value=bytes[0];
    if(format==CTR_VERTEX_SHORT) {
        value|=(int)bytes[1]<<8;if(value&0x8000)value-=65536;
    } else if(format==CTR_VERTEX_BYTE && value&0x80)value-=256;
    return (float)value;
}
static size_t vertex_id(const ctr_vertex_indices *indices,size_t first,size_t vertex) {
    if(!indices)return first+vertex;
    unsigned width=indices->format==CTR_VERTEX_INDEX_UNSIGNED_SHORT?2u:1u;
    const uint8_t *bytes=indices->bytes;
    size_t at=indices->offset+(first+vertex)*width;
    return width==2u?(size_t)bytes[at]|(size_t)bytes[at+1]<<8:bytes[at];
}
static ctr_vertex_result decode(const ctr_vertex_attribute *attributes,
                               size_t attribute_count,
                               const ctr_vertex_stream *streams,
                               size_t stream_count,const ctr_vertex_indices *indices,
                               size_t first,size_t vertex_count,ctr_vertex *output,
                               size_t capacity) {
    if(!attributes || !streams || (!output && capacity))return CTR_VERTEX_ARGUMENT;
    if(!attribute_count || !stream_count)return CTR_VERTEX_LAYOUT;
    if(attribute_count>CTR_VERTEX_MAX_ATTRIBUTES || stream_count>CTR_VERTEX_MAX_STREAMS ||
       vertex_count>CTR_VERTEX_MAX_COUNT)return CTR_VERTEX_LIMIT;
    size_t attribute_bytes=attribute_count*sizeof(*attributes),stream_bytes=stream_count*sizeof(*streams);
    size_t output_bytes=vertex_count*sizeof(*output);
    if(!span(attributes,attribute_bytes) || !span(streams,stream_bytes) ||
       (output && !span(output,output_bytes)) ||
       (indices && !span(indices,sizeof(*indices))))return CTR_VERTEX_OVERFLOW;
    if(output && capacity<vertex_count)return CTR_VERTEX_CAPACITY;
    if(overlaps(output,output?output_bytes:0,attributes,attribute_bytes) ||
       overlaps(output,output?output_bytes:0,streams,stream_bytes) ||
       (indices && overlaps(output,output?output_bytes:0,indices,sizeof(*indices))))
        return CTR_VERTEX_ALIAS;
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128)
        return CTR_VERTEX_UNSUPPORTED;
    float one=1.0f;uint32_t representation;
    memcpy(&representation,&one,sizeof(representation));
    if(representation!=UINT32_C(0x3f800000))return CTR_VERTEX_UNSUPPORTED;
    for(size_t s=0;s<stream_count;++s) {
        const ctr_vertex_stream *stream=&streams[s];
        if(!stream->stride || stream->stride>255u)return CTR_VERTEX_LAYOUT;
        if(stream->size>CTR_VERTEX_MAX_SOURCE_BYTES)return CTR_VERTEX_LIMIT;
        if(!span(stream->bytes,stream->size))return stream->bytes?CTR_VERTEX_OVERFLOW:CTR_VERTEX_ARGUMENT;
        if(stream->offset>stream->size)return CTR_VERTEX_RANGE;
        if(overlaps(output,output?output_bytes:0,stream->bytes,stream->size))return CTR_VERTEX_ALIAS;
    }
    size_t last=first;
    if(vertex_count && vertex_count-1>SIZE_MAX-first)return CTR_VERTEX_OVERFLOW;
    if(vertex_count)last+=vertex_count-1;
    size_t last_vertex=last;
    if(indices) {
        if(indices->format>CTR_VERTEX_INDEX_UNSIGNED_SHORT)return CTR_VERTEX_UNSUPPORTED;
        if(indices->size>CTR_VERTEX_MAX_SOURCE_BYTES)return CTR_VERTEX_LIMIT;
        if(!span(indices->bytes,indices->size))return indices->bytes?CTR_VERTEX_OVERFLOW:CTR_VERTEX_ARGUMENT;
        if(indices->offset>indices->size)return CTR_VERTEX_RANGE;
        if(overlaps(output,output?output_bytes:0,indices->bytes,indices->size))return CTR_VERTEX_ALIAS;
        if(vertex_count) {
            unsigned width=indices->format==CTR_VERTEX_INDEX_UNSIGNED_SHORT?2u:1u;
            if(last>SIZE_MAX/width)return CTR_VERTEX_OVERFLOW;
            size_t at=last*width;
            if(at>SIZE_MAX-indices->offset)return CTR_VERTEX_OVERFLOW;
            at+=indices->offset;
            if(at>indices->size || width>indices->size-at)return CTR_VERTEX_RANGE;
            last_vertex=0;
            for(size_t vertex=0;vertex<vertex_count;++vertex) {
                size_t id=vertex_id(indices,first,vertex);
                if(id>last_vertex)last_vertex=id;
            }
        }
    }
    uint16_t enabled=0;
    for(size_t a=0;a<attribute_count;++a) {
        const ctr_vertex_attribute *attribute=&attributes[a];
        if(attribute->format>CTR_VERTEX_FLOAT)return CTR_VERTEX_UNSUPPORTED;
        if(attribute->stream>=stream_count || attribute->input>=CTR_VERTEX_INPUTS ||
           !attribute->components || attribute->components>4u)return CTR_VERTEX_LAYOUT;
        uint16_t bit=(uint16_t)(1u<<attribute->input);
        if(enabled&bit)return CTR_VERTEX_LAYOUT;
        enabled|=bit;
        unsigned width=attribute->format==CTR_VERTEX_FLOAT?4u:attribute->format==CTR_VERTEX_SHORT?2u:1u;
        const ctr_vertex_stream *stream=&streams[attribute->stream];
        unsigned bytes=width*attribute->components;
        if(attribute->offset>stream->stride || bytes>stream->stride-attribute->offset)
            return CTR_VERTEX_LAYOUT;
        if(!vertex_count)continue;
        if(last_vertex>SIZE_MAX/stream->stride)return CTR_VERTEX_OVERFLOW;
        size_t offset=last_vertex*stream->stride;
        if(offset>SIZE_MAX-stream->offset)return CTR_VERTEX_OVERFLOW;
        offset+=stream->offset;
        if(offset>stream->size || attribute->offset>stream->size-offset ||
           bytes>stream->size-offset-attribute->offset)return CTR_VERTEX_RANGE;
        if(attribute->format==CTR_VERTEX_FLOAT) {
            /* Every value is checked, including later vertices; no valid prefix
             * can escape before a late NaN/Infinity is rejected. */
            const uint8_t *source=stream->bytes;
            for(size_t vertex=0;vertex<vertex_count;++vertex) {
                size_t at=stream->offset+vertex_id(indices,first,vertex)*stream->stride+attribute->offset;
                for(unsigned lane=0;lane<attribute->components;++lane)
                    if((little32(source+at+lane*4u)&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))
                        return CTR_VERTEX_UNSUPPORTED;
            }
        }
    }
    if(!output)return CTR_VERTEX_OK;
    for(size_t vertex=0;vertex<vertex_count;++vertex) {
        for(unsigned input=0;input<CTR_VERTEX_INPUTS;++input)
            for(unsigned lane=0;lane<4;++lane)output[vertex].input[input][lane]=lane==3?1.0f:0.0f;
        output[vertex].enabled=enabled;
        for(size_t a=0;a<attribute_count;++a) {
            const ctr_vertex_attribute *attribute=&attributes[a];
            const ctr_vertex_stream *stream=&streams[attribute->stream];
            unsigned width=attribute->format==CTR_VERTEX_FLOAT?4u:attribute->format==CTR_VERTEX_SHORT?2u:1u;
            size_t at=stream->offset+vertex_id(indices,first,vertex)*stream->stride+attribute->offset;
            const uint8_t *source=stream->bytes;
            for(unsigned lane=0;lane<attribute->components;++lane)
                output[vertex].input[attribute->input][lane]=component(source+at+lane*width,attribute->format);
        }
    }
    return CTR_VERTEX_OK;
}
ctr_vertex_result ctr_vertex_decode(const ctr_vertex_attribute *attributes,
                                   size_t attribute_count,
                                   const ctr_vertex_stream *streams,
                                   size_t stream_count,size_t first_vertex,
                                   size_t vertex_count,ctr_vertex *output,
                                   size_t capacity) {
    return decode(attributes,attribute_count,streams,stream_count,NULL,
                  first_vertex,vertex_count,output,capacity);
}
ctr_vertex_result ctr_vertex_decode_indexed(const ctr_vertex_attribute *attributes,
                                           size_t attribute_count,
                                           const ctr_vertex_stream *streams,
                                           size_t stream_count,
                                           const ctr_vertex_indices *indices,
                                           size_t first_index,size_t vertex_count,
                                           ctr_vertex *output,size_t capacity) {
    if(!indices)return CTR_VERTEX_ARGUMENT;
    return decode(attributes,attribute_count,streams,stream_count,indices,
                  first_index,vertex_count,output,capacity);
}
const char *ctr_vertex_result_name(ctr_vertex_result result) {
    static const char *const names[]={"ok","argument","layout","unsupported","range","limit","capacity","alias","overflow"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"unknown";
}
