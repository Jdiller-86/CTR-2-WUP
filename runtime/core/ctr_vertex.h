#ifndef CTR_VERTEX_H
#define CTR_VERTEX_H
#include <stddef.h>
#include <stdint.h>

#define CTR_VERTEX_MAX_ATTRIBUTES 12u
#define CTR_VERTEX_MAX_STREAMS 12u
#define CTR_VERTEX_INPUTS 16u
#define CTR_VERTEX_MAX_COUNT 4096u
#define CTR_VERTEX_MAX_SOURCE_BYTES (4u*1024u*1024u)

/* The four GPU_ATTRIBFMT component types, without integer normalization.
 * PICA has no unsigned-16 vertex attribute type. */
typedef enum {
    CTR_VERTEX_BYTE=0, CTR_VERTEX_UNSIGNED_BYTE=1,
    CTR_VERTEX_SHORT=2, CTR_VERTEX_FLOAT=3
} ctr_vertex_format;
typedef enum {
    CTR_VERTEX_OK, CTR_VERTEX_ARGUMENT, CTR_VERTEX_LAYOUT,
    CTR_VERTEX_UNSUPPORTED, CTR_VERTEX_RANGE, CTR_VERTEX_LIMIT,
    CTR_VERTEX_CAPACITY, CTR_VERTEX_ALIAS, CTR_VERTEX_OVERFLOW
} ctr_vertex_result;

typedef struct {
    const void *bytes; /* Caller-owned byte buffer; host alignment is immaterial. */
    size_t size, offset; /* offset is the first vertex, relative to bytes. */
    unsigned stride; /* Resolved loader byte count, 1..255. */
} ctr_vertex_stream;
typedef struct {
    unsigned stream, input, format, components;
    unsigned offset; /* Resolved attribute offset within one vertex's stride. */
} ctr_vertex_attribute;
typedef struct {
    float input[CTR_VERTEX_INPUTS][4];
    uint16_t enabled; /* Only these slots were supplied by the validated layout. */
} ctr_vertex;

/* Decode a contiguous vertex range from an explicit, resolved array layout.
 * Up to twelve attributes/streams map uniquely onto sixteen shader input slots;
 * each has 1..4 components. Missing lanes/slots are initialized to (0,0,0,1),
 * but unconfigured slots remain absent from enabled. Signed 8/16 and unsigned 8
 * values convert exactly to host float32. Float32 bytes are little-endian and
 * copied bit-exactly; nonfinite encodings and non-IEEE binary32 hosts fail.
 * No PICA float24 rounding, loader-register parsing, retained/fixed attributes,
 * index-buffer interpretation, guest memory access, shader or draw is performed.
 * NULL output with capacity zero validates only. All layout, source range,
 * float and alias checks complete before any output changes. Input and output
 * spans must be caller-owned and remain stable during the call. */
ctr_vertex_result ctr_vertex_decode(const ctr_vertex_attribute *attributes,
                                   size_t attribute_count,
                                   const ctr_vertex_stream *streams,
                                   size_t stream_count, size_t first_vertex,
                                   size_t vertex_count, ctr_vertex *output,
                                   size_t capacity);
const char *ctr_vertex_result_name(ctr_vertex_result result);
#endif
