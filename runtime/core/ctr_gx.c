/* Original bounded synchronous GX operations; no emulator or hardware backend. */
#include "ctr_gx.h"
#include "ctr_transfer.h"
#include "ctr_guest_pica.h"
#include "ctr_resource_cache.h"
#include <stdlib.h>
#include <string.h>
#define LIMIT (4u*1024u*1024u)
ctr_gx_result ctr_gx_check_range(ctr_guest *g,uint32_t address,uint32_t size,unsigned permissions,ctr_gx_completion *c) {
    c->detail=address;c->memory=ctr_memory_check_range(&g->memory,address,size,permissions);
    if(c->memory!=CTR_MEM_OK)return CTR_GX_MEMORY;
    if((permissions&CTR_MEM_WRITE) && g->gsp.registered && size) {
        const ctr_guest_shared *relay=&g->shared[g->gsp.relay_shared];uintptr_t base=(uintptr_t)relay->storage;
        uint64_t end=(uint64_t)address+size;
        for(size_t i=0;i<g->memory.count;++i) {
            const ctr_mapping *m=&g->memory.maps[i];uint64_t stop=(uint64_t)m->base+m->size;
            if(address>=stop || m->base>=end)continue;
            uint64_t first=address>m->base?address:m->base,last=end<stop?end:stop;
            uintptr_t p=(uintptr_t)m->storage+(size_t)(first-m->base);
            if(p<base+relay->size && base<p+(size_t)(last-first)) { c->memory=CTR_MEM_OVERLAP;return CTR_GX_MEMORY; }
        }
    }
    return CTR_GX_OK;
}
#define checked ctr_gx_check_range
static ctr_gx_result copied(ctr_guest *g,uint32_t source,uint32_t dest,uint32_t size,ctr_gx_completion *c) {
    if(!size || size>LIMIT)return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=checked(g,source,size,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=checked(g,dest,size,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *bytes=malloc(size);if(!bytes)return CTR_GX_NO_MEMORY;
    ctr_memory_read_bytes(&g->memory,source,bytes,size);ctr_memory_write_bytes(&g->memory,dest,bytes,size);
    free(bytes);return CTR_GX_OK;
}
static ctr_gx_result fill(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    uint8_t *bytes[2]={NULL,NULL};unsigned widths[2]={0,0};uint32_t sizes[2]={0,0};ctr_gx_result result=CTR_GX_OK;
    for(unsigned i=0;i<2;++i) {
        uint32_t begin=w[1+i*3],end=w[3+i*3],control=(w[7]>>(i*16))&65535;
        if(!begin) { if(end) { result=CTR_GX_UNSUPPORTED;break; }continue; }
        unsigned depth=(control>>8)&3;
        if(end<=begin || (begin&7) || (end&7) || !(control&1) || (control&~0x301u) || depth==3 || end-begin>LIMIT) { result=CTR_GX_UNSUPPORTED;break; }
        widths[i]=depth==0?2:depth==1?3:4;sizes[i]=end-begin;
        if(sizes[i]%widths[i]) { result=CTR_GX_UNSUPPORTED;break; }
        result=checked(g,begin,sizes[i],CTR_MEM_WRITE,c);if(result!=CTR_GX_OK)break;
        bytes[i]=malloc(sizes[i]);if(!bytes[i]) { result=CTR_GX_NO_MEMORY;break; }
        for(uint32_t k=0;k<sizes[i];++k)bytes[i][k]=(uint8_t)(w[2+i*3]>>((k%widths[i])*8));
    }
    if(result==CTR_GX_OK)for(unsigned i=0;i<2;++i)if(bytes[i])ctr_memory_write_bytes(&g->memory,w[1+i*3],bytes[i],sizes[i]);
    /* GSP dispatches one PSC0 for both banks; bank 1 alone dispatches PSC1. */
    c->signal=w[1]!=0 || w[4]!=0;c->irq=w[1]?0:1;
    free(bytes[0]);free(bytes[1]);return result;
}
static ctr_gx_result display(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    ctr_transfer t;if(w[6] || w[7] || (w[1]&7) || (w[2]&7) || !ctr_transfer_init(&t,w[3],w[4],w[5]))return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=checked(g,w[1],(uint32_t)t.input_bytes,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=checked(g,w[2],(uint32_t)t.output_bytes,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *input=malloc(t.input_bytes),*output=malloc(t.output_bytes);
    if(!input || !output) { free(input);free(output);return CTR_GX_NO_MEMORY; }
    ctr_memory_read_bytes(&g->memory,w[1],input,(uint32_t)t.input_bytes);
    ctr_transfer_convert(w[3],w[4],w[5],input,t.input_bytes,output,t.output_bytes);
    ctr_memory_write_bytes(&g->memory,w[2],output,(uint32_t)t.output_bytes);
    free(input);free(output);return CTR_GX_OK;
}
static bool span(uint32_t dim,uint32_t size,uint32_t *extent) {
    uint32_t width=(dim&65535)*16,gap=(dim>>16)*16;
    if(!width && gap)return false;
    uint64_t total=(uint64_t)size+(width?(uint64_t)((size-1)/width)*gap:0);
    if(total>LIMIT)return false;
    *extent=(uint32_t)total;return true;
}
static ctr_gx_result texture_copy(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    uint32_t size=w[3],in_span,out_span;
    if(!size || size>LIMIT || (size&15) || (w[1]&7) || (w[2]&7) || w[6]!=8 || w[7] ||
       !span(w[4],size,&in_span) || !span(w[5],size,&out_span))return CTR_GX_UNSUPPORTED;
    ctr_gx_result r=checked(g,w[1],in_span,CTR_MEM_READ,c);if(r!=CTR_GX_OK)return r;
    r=checked(g,w[2],out_span,CTR_MEM_WRITE,c);if(r!=CTR_GX_OK)return r;
    uint8_t *input=malloc(in_span),*bytes=malloc(size);
    if(!input || !bytes) { free(input);free(bytes);return CTR_GX_NO_MEMORY; }
    ctr_memory_read_bytes(&g->memory,w[1],input,in_span);
    uint32_t in_width=(w[4]&65535)*16,in_gap=(w[4]>>16)*16;
    for(uint32_t k=0;k<size;++k)bytes[k]=input[k+(in_width?(k/in_width)*in_gap:0)];
    uint32_t width=(w[5]&65535)*16,gap=(w[5]>>16)*16;
    for(uint32_t k=0;k<size;) {
        uint32_t count=width?width:size;if(count>size-k)count=size-k;
        ctr_memory_write_bytes(&g->memory,w[2]+k+(width?(k/width)*gap:0),bytes+k,count);k+=count;
    }
    free(input);free(bytes);return CTR_GX_OK;
}
static ctr_gx_result pica_list(ctr_guest *g,const uint32_t *w,ctr_gx_completion *c) {
    /* The GX command-list address is virtual; PICA resource/target addresses
       inside it are physical and pass through the separate owned resolver. */
    c->signal=false;
    uint32_t size=w[2];
    if(!size || size>CTR_PICA_MAX_BYTES || (size&15u) || (w[1]&7u) ||
       w[3] || w[4] || w[5] || w[6] || w[7]>1)return CTR_GX_UNSUPPORTED;
    ctr_gx_result result=checked(g,w[1],size,CTR_MEM_READ,c);
    if(result!=CTR_GX_OK)return result;
    if(!ctr_guest_pica_prepare_caches(g))return CTR_GX_NO_MEMORY;
    ctr_resource_lease commands={0};
    c->memory=ctr_resource_cache_acquire(g->pica_resources,CTR_RESOURCE_COMMAND,w[1],size,CTR_MEM_READ,&commands);
    if(c->memory!=CTR_MEM_OK)return c->memory==CTR_MEM_CAPACITY?CTR_GX_NO_MEMORY:CTR_GX_MEMORY;
    const uint8_t *bytes=commands.bytes;
    if(w[7])ctr_cpu_barrier();
    ctr_pica_report report={0};
    ctr_pica_result parsed=ctr_pica_execute(&g->gsp.pica,bytes,size,CTR_PICA_MAX_WRITES,&report);
    g->gsp.last_pica_result=(uint32_t)parsed;
    g->gsp.last_pica_offset=(uint32_t)report.parameter_offset;
    g->gsp.last_pica_register=report.reg;
    if(parsed==CTR_PICA_UNSUPPORTED) {
        result=ctr_guest_pica_execute(g,bytes,size,w[1],c);
        ctr_resource_cache_release(&commands);
        if(result!=CTR_GX_OK)return result;
        parsed=CTR_PICA_OK;
    } else {
        ctr_resource_cache_release(&commands);
        if(parsed==CTR_PICA_OK) {
            g->gsp.last_scene_result=CTR_SCENE_OK;g->gsp.last_vertex_result=CTR_VERTEX_OK;
            g->gsp.last_shader_result=CTR_SHADER_OK;
        }
        if(parsed==CTR_PICA_OK && g->gsp.pica_scene) {
            memcpy(g->gsp.pica_scene->registers,g->gsp.pica.registers,sizeof(g->gsp.pica.registers));
            g->gsp.pica_scene->lists=g->gsp.pica.committed_lists;
        }
    }
    if(parsed!=CTR_PICA_OK) {
        c->detail=w[1]+(report.parameter_offset<size?(uint32_t)report.parameter_offset:0);
        return CTR_GX_UNSUPPORTED;
    }
    /* One synchronous command-list completion, distinct from the one/two
       canonical tail-marker writes. No asynchronous hardware timing claimed. */
    c->irq=5;c->signal=true;return CTR_GX_OK;
}
ctr_gx_result ctr_gx_execute(ctr_guest *g,const uint32_t w[8],ctr_gx_completion *c) {
    if(!g || !w || !c)return CTR_GX_UNSUPPORTED;
    *c=(ctr_gx_completion){.detail=w[0],.signal=true};unsigned id=w[0]&255;
    uint32_t header=w[0]&~UINT32_C(0x100ff); /* low ID and documented stop byte. */
    if((id==5?header!=0x00000100:header!=0x01000100) || ((w[0]>>16)&255)>1)return CTR_GX_UNSUPPORTED;
    ctr_gx_result result=CTR_GX_UNSUPPORTED;
    if(id==0 && !w[4] && !w[5] && !w[6] && !w[7]) { c->irq=6;result=copied(g,w[1],w[2],w[3],c); }
    else if(id==1)result=pica_list(g,w,c);
    else if(id==2)result=fill(g,w,c);
    else if(id==3) { c->irq=4;result=display(g,w,c); }
    else if(id==4) { c->irq=4;result=texture_copy(g,w,c); }
    else if(id==5 && !w[7]) {
        c->signal=false;result=CTR_GX_OK;
        for(unsigned i=0;i<3;++i) {
            uint32_t size=w[2+i*2];if(size>LIMIT) { result=CTR_GX_UNSUPPORTED;break; }
            if(size) { result=checked(g,w[1+i*2],size,CTR_MEM_READ,c);if(result!=CTR_GX_OK)break; }
        }
        if(result==CTR_GX_OK)ctr_cpu_barrier();
    }
    if(result==CTR_GX_OK && id!=5)g->cpu.exclusive_valid=false;
    return result;
}
