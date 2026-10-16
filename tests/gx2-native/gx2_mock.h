#ifndef CTR_GX2_NATIVE_MOCK_H
#define CTR_GX2_NATIVE_MOCK_H
#include <stdint.h>
#include <stdbool.h>
/* API model for resource lifetime and byte flow only; it does not emulate a
 * shader, rasterizer, GX2 driver, cache coherency or physical Wii U execution. */
typedef unsigned GX2TileMode;
struct GX2ContextState { uint32_t marker; };
typedef struct {
    unsigned dim,width,height,depth,mipLevels,format,aa,use,imageSize;
    void *image;unsigned mipmapSize,tileMode,alignment,pitch;
} GX2Surface;
typedef struct { GX2Surface surface;unsigned viewNumSlices; } GX2ColorBuffer;
typedef struct { unsigned unused; } GX2FetchShader;
typedef struct {
    struct {
        unsigned sq_pgm_resources_vs,num_spi_vs_out_id,spi_vs_out_id[10];
        unsigned sq_vtx_semantic_clear,num_sq_vtx_semantic,sq_vtx_semantic[32];
        unsigned vgt_vertex_reuse_block_cntl,vgt_hos_reuse_depth;
    } regs;
    unsigned size;void *program;unsigned mode;
} GX2VertexShader;
typedef struct {
    struct {
        unsigned sq_pgm_resources_ps,sq_pgm_exports_ps,spi_ps_in_control_0;
        unsigned num_spi_ps_input_cntl,spi_ps_input_cntls[32];
        unsigned cb_shader_mask,cb_shader_control,db_shader_control;
    } regs;
    unsigned size;void *program;unsigned mode;
} GX2PixelShader;
typedef struct { unsigned location,buffer,offset,format,type,mask,endianSwap; } GX2AttribStream;
typedef struct {
    uint32_t pa_cl_vport_xscale,pa_cl_vport_xoffset,pa_cl_vport_yscale,pa_cl_vport_yoffset;
} GX2ViewportReg;
enum {
    GX2_SURFACE_DIM_TEXTURE_2D=1,GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8=0x1a,
    GX2_AA_MODE1X=0,GX2_SURFACE_USE_TEXTURE=1,GX2_SURFACE_USE_COLOR_BUFFER=2,
    GX2_TILE_MODE_TILED_2D_THIN1=4,GX2_TILE_MODE_LINEAR_ALIGNED=1,
    GX2_INVALIDATE_MODE_CPU_TEXTURE=66,GX2_INVALIDATE_MODE_COLOR_BUFFER=16,
    GX2_INVALIDATE_MODE_CPU_SHADER=72,GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER=65,
    GX2_FETCH_SHADER_TESSELLATION_NONE=0,GX2_TESSELLATION_MODE_DISCRETE=0,
    GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32=0x813,GX2_ATTRIB_INDEX_PER_VERTEX=0,
    GX2_SQ_SEL_X=0,GX2_SQ_SEL_Y=1,GX2_SQ_SEL_Z=2,GX2_SQ_SEL_W=3,
    GX2_ENDIAN_SWAP_8_IN_32=2,GX2_SHADER_MODE_UNIFORM_REGISTER=0,
    GX2_PRIMITIVE_MODE_TRIANGLES=4,GX2_RENDER_TARGET_0=0,GX2_LOGIC_OP_COPY=0xcc,
    GX2_CHANNEL_MASK_RGBA=15,GX2_COMPARE_FUNC_ALWAYS=7,GX2_STENCIL_FUNCTION_KEEP=0,
    GX2_ALPHA_TO_MASK_MODE_NON_DITHERED=0,GX2_FRONT_FACE_CCW=0
};
#define GX2_SEL_MASK(x,y,z,w) (((x)<<24)|((y)<<16)|((z)<<8)|(w))
void GX2SetContextState(GX2ContextState *context);
bool GX2DrawDone(void);
void GX2CalcSurfaceSizeAndAlignment(GX2Surface *surface);
void GX2CopySurface(const GX2Surface *src,unsigned a,unsigned b,GX2Surface *dst,unsigned c,unsigned d);
void DCFlushRange(void *bytes,unsigned size);
void DCInvalidateRange(void *bytes,unsigned size);
void GX2Invalidate(unsigned mode,void *bytes,unsigned size);
unsigned GX2CalcFetchShaderSizeEx(unsigned count,unsigned type,unsigned tess);
void GX2InitFetchShaderEx(GX2FetchShader *fetch,void *bytes,unsigned count,const GX2AttribStream *attributes,unsigned type,unsigned tess);
void GX2InitViewportReg(GX2ViewportReg *v,float x,float y,float w,float h,float n,float f);
void GX2SetViewportReg(const GX2ViewportReg *v);
void GX2SetRasterizerClipControlEx(bool raster,bool clip,bool half);
void GX2SetScissor(unsigned x,unsigned y,unsigned w,unsigned h);
void GX2DrawEx(unsigned type,unsigned count,unsigned first,unsigned instances);
void GX2SetColorBuffer(const GX2ColorBuffer *target,unsigned index);
void GX2SetShaderModeEx(unsigned mode,unsigned vs,unsigned vsstack,unsigned gs,unsigned gsstack,unsigned ps,unsigned psstack);
void GX2SetVertexShader(const GX2VertexShader *shader);
void GX2SetPixelShader(const GX2PixelShader *shader);
void GX2SetAttribBuffer(unsigned index,unsigned size,unsigned stride,const void *bytes);
void GX2SetVertexUniformReg(unsigned offset,unsigned count,const void *bytes);
void GX2SetCullOnlyControl(unsigned front,bool cullfront,bool cullback);
#define GX2SetupContextStateEx(p,flag) ((void)(p),(void)(flag))
#define GX2InitColorBufferRegs(p) ((void)(p))
#define GX2SetFetchShader(p) ((void)(p))
#define GX2SetColorControl(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
#define GX2SetTargetChannelMasks(a,b,c,d,e,f,g,h) ((void)0)
#define GX2SetDepthStencilControl(a,b,c,d,e,f,g,h,i,j,k,l,m) ((void)0)
#define GX2SetAlphaTest(a,b,c) ((void)(a),(void)(b),(void)(c))
#define GX2SetAlphaToMask(a,b) ((void)(a),(void)(b))
#define GX2SetStreamOutEnable(a) ((void)(a))
#endif
