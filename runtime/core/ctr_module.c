/* Original bounded CRO0/CRS loader. Wire layout facts: documented CRO0 and the
 * audited Azahar RO interface; no upstream implementation is incorporated. */
#include "ctr_module.h"
#include <stdlib.h>
#include <string.h>

#define HEADER 0x138u
#define MAX_SYMBOLS 128u
#define MAX_RELOCS 1024u
#define NO_ALLOCATION CTR_GUEST_MAX_ALLOCATIONS
/* Word offsets from the CRO hash prefix, 0x80. */
enum { MAGIC,NAME,NEXT,PREVIOUS,FILE_SIZE,BSS_SIZE,FIXED,ZERO,
    UNKNOWN_TAG,LOAD_TAG,EXIT_TAG,UNRESOLVED_TAG,CODE,CODE_SIZE,DATA,DATA_SIZE,
    MODULE_NAME,MODULE_NAME_SIZE,SEGMENTS,SEGMENT_COUNT,EXPORTS,EXPORT_COUNT,
    INDEX_EXPORTS,INDEX_EXPORT_COUNT,EXPORT_STRINGS,EXPORT_STRINGS_SIZE,TREE,TREE_COUNT,
    IMPORT_MODULES,IMPORT_MODULE_COUNT,EXTERNAL,EXTERNAL_COUNT,IMPORTS,IMPORT_COUNT,
    INDEX_IMPORTS,INDEX_IMPORT_COUNT,ANON_IMPORTS,ANON_IMPORT_COUNT,IMPORT_STRINGS,IMPORT_STRINGS_SIZE,
    STATIC_ANON,STATIC_ANON_COUNT,INTERNAL,INTERNAL_COUNT,STATIC_RELOCS,STATIC_RELOC_COUNT,FIELDS };
typedef struct { uint32_t offset,size,type,address; } segment;
typedef struct {
    const ctr_module_image *image;
    uint32_t h[FIELDS];
    segment segments[4];
    uint8_t *file;
    uint32_t source,dependencies;
    unsigned allocations[2];
    bool linked,automatic;
} module;
typedef struct ctr_module_loader {
    const ctr_module_image *images;
    size_t count;
    module loaded[CTR_MODULE_MAX_IMAGES];
    uint64_t generation;
    uint64_t loads,links,unloads,failures;
    ctr_module_result last;
} loader;
typedef struct { uint8_t *file,*data,*bss;uint32_t dependencies; } staging;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void write32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(8*i));
}
static bool span(uint32_t offset,uint64_t size,uint32_t limit) {
    return offset<=limit && size<=(uint64_t)limit-offset;
}
static bool overlaps(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return n && m && (uint64_t)a+n>b && (uint64_t)b+m>a;
}
static uint32_t field(const module *m,unsigned f) { return m->h[f]; }
static uint32_t segment_size(const module *m,unsigned type);
static const uint8_t *raw(const module *m,unsigned f,unsigned index,unsigned stride) {
    return m->image->bytes+field(m,f)+(size_t)index*stride;
}
static const char *string_at(const module *m,uint32_t at,unsigned table,unsigned length) {
    uint32_t begin=field(m,table),size=field(m,length);
    if(at<begin || !span(at-begin,1,size))return NULL;
    const char *s=(const char *)m->image->bytes+at;
    for(unsigned i=0;i<128 && i<size-(at-begin);++i) {
        unsigned c=(unsigned char)s[i];if(!c)return i?s:NULL;
        if(c<32 || c>126)return NULL;
    }
    return NULL;
}
static const char *module_name(const module *m) {
    return string_at(m,field(m,MODULE_NAME),MODULE_NAME,MODULE_NAME_SIZE);
}
static bool tag_address(const module *m,uint32_t tag,unsigned bytes,uint32_t *address) {
    unsigned index=tag&15u;uint32_t offset=tag>>4;
    if(index>=field(m,SEGMENT_COUNT) || !span(offset,bytes,m->segments[index].size) ||
        (uint64_t)m->segments[index].address+offset+bytes>UINT64_C(0x100000000))return false;
    *address=m->segments[index].address+offset;return true;
}
static bool table_slice(const module *m,uint32_t at,uint32_t count,unsigned table,unsigned num,unsigned stride) {
    uint32_t begin=field(m,table);
    return at>=begin && !((at-begin)%stride) && span(at-begin,(uint64_t)count*stride,field(m,num)*stride);
}
static ctr_module_result parse(module *m,const ctr_module_image *image) {
    memset(m,0,sizeof(*m));m->image=image;m->allocations[0]=m->allocations[1]=NO_ALLOCATION;
    if(!image->bytes || image->size<HEADER || image->size>CTR_MODULE_MAX_FILE ||
        image->size%CTR_GUEST_PAGE || image->address%CTR_GUEST_PAGE ||
        image->address<0x00100000u || (uint64_t)image->address+image->size>0x08000000u)
        return CTR_MODULE_FORMAT;
    for(unsigned i=0;i<FIELDS;++i)m->h[i]=read32(image->bytes+0x80+i*4u);
    if(field(m,MAGIC)!=0x304f5243u || field(m,FILE_SIZE)!=image->size ||
        field(m,NEXT) || field(m,PREVIOUS) || field(m,FIXED) || field(m,ZERO))return CTR_MODULE_FORMAT;
    const unsigned tables[][3]={{CODE,CODE_SIZE,1},{DATA,DATA_SIZE,1},{MODULE_NAME,MODULE_NAME_SIZE,1},
        {SEGMENTS,SEGMENT_COUNT,12},{EXPORTS,EXPORT_COUNT,8},{INDEX_EXPORTS,INDEX_EXPORT_COUNT,4},
        {EXPORT_STRINGS,EXPORT_STRINGS_SIZE,1},{TREE,TREE_COUNT,8},{IMPORT_MODULES,IMPORT_MODULE_COUNT,20},
        {EXTERNAL,EXTERNAL_COUNT,12},{IMPORTS,IMPORT_COUNT,8},{INDEX_IMPORTS,INDEX_IMPORT_COUNT,8},
        {ANON_IMPORTS,ANON_IMPORT_COUNT,8},{IMPORT_STRINGS,IMPORT_STRINGS_SIZE,1},
        {STATIC_ANON,STATIC_ANON_COUNT,8},{INTERNAL,INTERNAL_COUNT,12},{STATIC_RELOCS,STATIC_RELOC_COUNT,12}};
    for(unsigned i=0;i<sizeof(tables)/sizeof(*tables);++i) {
        unsigned f=tables[i][0],n=tables[i][1],stride=tables[i][2];uint64_t bytes=(uint64_t)field(m,n)*stride;
        if(!span(field(m,f),bytes,image->size) || (bytes && field(m,f)<HEADER) || (stride>1 && (field(m,f)&3u)))
            return CTR_MODULE_FORMAT;
        for(unsigned j=0;bytes && j<i;++j) {
            uint64_t other=(uint64_t)field(m,tables[j][1])*tables[j][2];
            if(other && field(m,f)<(uint64_t)field(m,tables[j][0])+other && field(m,tables[j][0])<(uint64_t)field(m,f)+bytes)
                return CTR_MODULE_FORMAT;
        }
    }
    if(!field(m,SEGMENT_COUNT) || field(m,SEGMENT_COUNT)>4 || field(m,EXPORT_COUNT)>MAX_SYMBOLS ||
        field(m,INDEX_EXPORT_COUNT)>MAX_SYMBOLS || field(m,IMPORT_COUNT)>MAX_SYMBOLS ||
        field(m,INDEX_IMPORT_COUNT)>MAX_SYMBOLS || field(m,ANON_IMPORT_COUNT)>MAX_SYMBOLS ||
        field(m,IMPORT_MODULE_COUNT)>CTR_MODULE_MAX_IMAGES || field(m,TREE_COUNT)>MAX_SYMBOLS*2 ||
        field(m,EXTERNAL_COUNT)>MAX_RELOCS || field(m,INTERNAL_COUNT)>MAX_RELOCS)return CTR_MODULE_CAPACITY;
    if(field(m,STATIC_ANON_COUNT) || field(m,STATIC_RELOC_COUNT))return CTR_MODULE_UNSUPPORTED;
    if(!module_name(m) || (field(m,NAME) && !string_at(m,field(m,NAME),MODULE_NAME,MODULE_NAME_SIZE)))return CTR_MODULE_FORMAT;
    unsigned types=0;
    for(unsigned i=0;i<field(m,SEGMENT_COUNT);++i) {
        const uint8_t *p=raw(m,SEGMENTS,i,12);segment *s=&m->segments[i];
        *s=(segment){read32(p),read32(p+4),read32(p+8),0};
        if(s->type>3 || (types&(1u<<s->type)))return CTR_MODULE_FORMAT;
        types|=1u<<s->type;
        if(image->is_static) {
            s->address=s->offset;
            if((uint64_t)s->address+s->size>0x40000000u)return CTR_MODULE_FORMAT;
        } else if(s->type<2) {
            if(!span(s->offset,s->size,image->size) || (s->size && s->offset<HEADER))return CTR_MODULE_FORMAT;
            if(s->type==0 && (s->offset!=field(m,CODE) || s->size!=field(m,CODE_SIZE)))return CTR_MODULE_FORMAT;
            s->address=image->address+s->offset;
        } else if(s->type==2) {
            if(s->offset!=field(m,DATA) || s->size!=field(m,DATA_SIZE))return CTR_MODULE_FORMAT;
            s->address=image->data_address;
        } else {
            if(s->offset || s->size!=field(m,BSS_SIZE))return CTR_MODULE_FORMAT;
            s->address=image->bss_address;
        }
        if((s->address&3u) || (uint64_t)s->address+s->size>0x40000000u)return CTR_MODULE_FORMAT;
        if(s->size>CTR_MODULE_MAX_FILE)return CTR_MODULE_CAPACITY;
        for(unsigned j=0;j<i;++j)if(overlaps(s->address,s->size,m->segments[j].address,m->segments[j].size))return CTR_MODULE_FORMAT;
    }
    if(!image->is_static && (!(types&1u) || !field(m,CODE_SIZE) ||
        (field(m,CODE)|field(m,CODE_SIZE))&3u || image->code_size!=field(m,CODE_SIZE) || !image->code ||
        (!image->blocks && image->block_count)))return CTR_MODULE_FORMAT;
    if(image->is_static && (image->block_count || field(m,INTERNAL_COUNT) || field(m,EXTERNAL_COUNT) ||
        field(m,IMPORT_MODULE_COUNT) || field(m,IMPORT_COUNT) || field(m,INDEX_IMPORT_COUNT) || field(m,ANON_IMPORT_COUNT)))return CTR_MODULE_UNSUPPORTED;
    if(!image->is_static && (segment_size(m,2)!=field(m,DATA_SIZE) || segment_size(m,3)!=field(m,BSS_SIZE)))return CTR_MODULE_FORMAT;
    for(size_t i=0;i<image->block_count;++i) {
        const ctr_block *b=&image->blocks[i];unsigned width=b->thumb?2u:4u;
        if(!b->run || b->address<image->address+field(m,CODE) ||
            (uint64_t)b->address+width>(uint64_t)image->address+field(m,CODE)+field(m,CODE_SIZE) ||
            b->address%width || (i && image->blocks[i-1].address>=b->address))return CTR_MODULE_FORMAT;
    }
    for(unsigned i=0;i<field(m,EXPORT_COUNT);++i) {
        const uint8_t *p=raw(m,EXPORTS,i,8);uint32_t address;
        const char *name=string_at(m,read32(p),EXPORT_STRINGS,EXPORT_STRINGS_SIZE);
        if(!name || !tag_address(m,read32(p+4),1,&address))return CTR_MODULE_FORMAT;
        for(unsigned j=0;j<i;++j)if(!strcmp(name,string_at(m,read32(raw(m,EXPORTS,j,8)),EXPORT_STRINGS,EXPORT_STRINGS_SIZE)))return CTR_MODULE_SYMBOL;
    }
    for(unsigned i=0;i<field(m,INDEX_EXPORT_COUNT);++i) {
        uint32_t address;if(!tag_address(m,read32(raw(m,INDEX_EXPORTS,i,4)),1,&address))return CTR_MODULE_FORMAT;
    }
    for(unsigned i=0;i<field(m,TREE_COUNT);++i) {
        const uint8_t *p=raw(m,TREE,i,8);
        unsigned left=((unsigned)p[2]|(unsigned)p[3]<<8)&0x7fffu,right=((unsigned)p[4]|(unsigned)p[5]<<8)&0x7fffu;
        if(left>=field(m,TREE_COUNT) || right>=field(m,TREE_COUNT) ||
            ((unsigned)p[6]|(unsigned)p[7]<<8)>=field(m,EXPORT_COUNT))return CTR_MODULE_FORMAT;
    }
    return CTR_MODULE_OK;
}

static bool export_named(const module *m,const char *name,uint32_t *address) {
    for(unsigned i=0;i<field(m,EXPORT_COUNT);++i) {
        const uint8_t *p=raw(m,EXPORTS,i,8);
        if(!strcmp(name,string_at(m,read32(p),EXPORT_STRINGS,EXPORT_STRINGS_SIZE)))return tag_address(m,read32(p+4),1,address);
    }
    return false;
}
static uint8_t *target(const module *m,staging *s,uint32_t tag,uint32_t *address) {
    if(!tag_address(m,tag,4,address) || (*address&3u))return NULL;
    const segment *seg=&m->segments[tag&15u];uint32_t offset=tag>>4;
    if(m->image->is_static)return NULL;
    return seg->type==2?s->data+offset:seg->type==3?s->bss+offset:s->file+seg->offset+offset;
}
static ctr_module_result relocate(const module *m,staging *s,const uint8_t *p,uint32_t symbol,bool *seen) {
    uint32_t address;uint8_t *dest=target(m,s,read32(p),&address);
    if(!dest)return CTR_MODULE_FORMAT;
    /* A relocation has one owner; duplicate/overlapping targets are ambiguous. */
    uint32_t tag=read32(p),index=(tag>>4)/4;
    unsigned seg=tag&15u;size_t key=0;
    for(unsigned i=0;i<seg;++i)key+=(m->segments[i].size+3u)/4u;
    key+=index;if(seen[key])return CTR_MODULE_FORMAT;seen[key]=true;
    unsigned type=p[4];uint32_t value=symbol+read32(p+8);
    if(type==0)return CTR_MODULE_OK;
    if(type==3)value-=address;
    else if(type!=2 && type!=38)return CTR_MODULE_UNSUPPORTED;
    write32(dest,value);return CTR_MODULE_OK;
}
static ctr_module_result batch(const module *m,staging *s,uint32_t offset,uint32_t symbol,bool *used,bool *seen) {
    uint32_t first=field(m,EXTERNAL),count=field(m,EXTERNAL_COUNT);
    if(offset<first || (offset-first)%12 || (offset-first)/12>=count)return CTR_MODULE_FORMAT;
    for(unsigned i=(offset-first)/12;i<count;++i) {
        const uint8_t *p=raw(m,EXTERNAL,i,12);
        if(used[i] || p[5]>1 || p[6] || p[7])return CTR_MODULE_FORMAT;
        used[i]=true;ctr_module_result result=relocate(m,s,p,symbol,seen);
        if(result!=CTR_MODULE_OK)return result;
        s->file[field(m,EXTERNAL)+i*12+6]=1;
        if(p[5])return CTR_MODULE_OK;
    }
    return CTR_MODULE_FORMAT;
}
static ctr_module_result apply_links(loader *l,module *m,staging *s,bool link) {
    size_t words=0;for(unsigned i=0;i<field(m,SEGMENT_COUNT);++i)words+=(m->segments[i].size+3u)/4u;
    bool *seen=calloc(words?words:1,sizeof(bool));if(!seen)return CTR_MODULE_CAPACITY;
    bool used[MAX_RELOCS]={false};ctr_module_result result=CTR_MODULE_OK;
    for(unsigned i=0;i<field(m,INTERNAL_COUNT);++i) {
        const uint8_t *p=raw(m,INTERNAL,i,12);
        if(p[5]>=field(m,SEGMENT_COUNT) || p[6] || p[7]) { result=CTR_MODULE_FORMAT;goto done; }
        result=relocate(m,s,p,m->segments[p[5]].address,seen);if(result)goto done;
    }
    for(unsigned i=0;i<field(m,IMPORT_COUNT);++i) {
        const uint8_t *p=raw(m,IMPORTS,i,8);
        const char *name=string_at(m,read32(p),IMPORT_STRINGS,IMPORT_STRINGS_SIZE);
        if(!name) { result=CTR_MODULE_FORMAT;goto done; }
        uint32_t address=0;unsigned matches=0;
        if(link)for(unsigned j=0;j<l->count;++j) {
            module *provider=&l->loaded[j];uint32_t value;
            if(provider->file && provider!=m && provider->linked && export_named(provider,name,&value)) {
                address=value;++matches;s->dependencies|=1u<<j;
            }
        }
        if(link && matches!=1) { result=CTR_MODULE_SYMBOL;goto done; }
        result=batch(m,s,read32(p+4),address,used,seen);if(result)goto done;
    }
    bool index_used[MAX_SYMBOLS]={false},anon_used[MAX_SYMBOLS]={false};
    for(unsigned i=0;i<field(m,IMPORT_MODULE_COUNT);++i) {
        const uint8_t *p=raw(m,IMPORT_MODULES,i,20);const char *name=string_at(m,read32(p),IMPORT_STRINGS,IMPORT_STRINGS_SIZE);
        if(!name) { result=CTR_MODULE_FORMAT;goto done; }
        module *provider=NULL;unsigned provider_index=0;
        if(link)for(unsigned j=0;j<l->count;++j) {
            module *candidate=&l->loaded[j];
            if(candidate->file && candidate!=m && candidate->linked && !strcmp(module_name(candidate),name)) {
                if(provider) { result=CTR_MODULE_SYMBOL;goto done; }
                provider=candidate;provider_index=j;
            }
        }
        if(link && !provider) { result=CTR_MODULE_SYMBOL;goto done; }
        if(link)s->dependencies|=1u<<provider_index;
        for(unsigned kind=0;kind<2;++kind) {
            unsigned table=kind?ANON_IMPORTS:INDEX_IMPORTS,num=kind?ANON_IMPORT_COUNT:INDEX_IMPORT_COUNT;
            uint32_t at=read32(p+4+kind*8),count=read32(p+8+kind*8);
            if(!count && !at)continue;
            if(!table_slice(m,at,count,table,num,8)) { result=CTR_MODULE_FORMAT;goto done; }
            for(unsigned j=(at-field(m,table))/8;j<(at-field(m,table))/8+count;++j) {
                bool *owned=kind?anon_used:index_used;
                if(owned[j]) { result=CTR_MODULE_FORMAT;goto done; }owned[j]=true;
                const uint8_t *entry=raw(m,table,j,8);uint32_t symbol=0,value=read32(entry);
                if(link) {
                    if(!kind) {
                        if(value>=field(provider,INDEX_EXPORT_COUNT)) { result=CTR_MODULE_SYMBOL;goto done; }
                        value=read32(raw(provider,INDEX_EXPORTS,value,4));
                    }
                    if(!tag_address(provider,value,1,&symbol)) { result=CTR_MODULE_SYMBOL;goto done; }
                }
                result=batch(m,s,read32(entry+4),symbol,used,seen);if(result)goto done;
            }
        }
    }
    for(unsigned i=0;i<field(m,EXTERNAL_COUNT);++i)if(!used[i]) { result=CTR_MODULE_FORMAT;goto done; }
    for(unsigned i=0;i<field(m,INDEX_IMPORT_COUNT);++i)if(!index_used[i]) { result=CTR_MODULE_FORMAT;goto done; }
    for(unsigned i=0;i<field(m,ANON_IMPORT_COUNT);++i)if(!anon_used[i]) { result=CTR_MODULE_FORMAT;goto done; }
done:
    free(seen);return result;
}

size_t ctr_modules_bytes(const ctr_guest *g) {
    const loader *l=g->modules;size_t bytes=0;if(!l)return 0;
    for(size_t i=0;i<l->count;++i)if(l->loaded[i].file)bytes+=l->loaded[i].image->size;
    return bytes;
}
bool ctr_modules_generation_current(const ctr_guest *g) {
    return g->modules && g->modules->generation==g->memory.code_generation &&
        g->program.code_generation==g->memory.code_generation;
}
bool ctr_modules_configure(ctr_guest *g,const ctr_module_image *images,size_t count) {
    if(g->modules || !images || !count || count>CTR_MODULE_MAX_IMAGES || g->cpu.instructions || g->ipc_requests ||
        g->state!=CTR_GUEST_READY || g->program.code_generation!=g->memory.code_generation)return false;
    unsigned statics=0;
    for(size_t i=0;i<count;++i) {
        module m;if(parse(&m,&images[i])!=CTR_MODULE_OK || !images[i].identity)return false;
        statics+=images[i].is_static;
        for(size_t j=0;j<i;++j) {
            module other;if(parse(&other,&images[j])!=CTR_MODULE_OK)return false;
            if(!strcmp(module_name(&m),module_name(&other)) ||
                overlaps(images[i].address,images[i].size,images[j].address,images[j].size))return false;
        }
    }
    if(statics!=1)return false;
    loader *l=calloc(1,sizeof(*l));if(!l)return false;
    l->images=images;l->count=count;l->generation=g->memory.code_generation;
    g->modules=l;return true;
}
void ctr_modules_close(ctr_guest *g) {
    loader *l=g->modules;if(!l)return;
    for(size_t i=0;i<l->count;++i) {
        module *m=&l->loaded[i];if(!m->file)continue;
        for(unsigned j=0;j<2;++j)if(m->allocations[j]<NO_ALLOCATION && g->allocations[m->allocations[j]].pins)
            --g->allocations[m->allocations[j]].pins;
        free(m->file);
    }
    free(l);g->modules=NULL;
}
static void release_stage(staging *s) { free(s->file);free(s->data);free(s->bss);memset(s,0,sizeof(*s)); }
static unsigned allocation_for(ctr_guest *g,uint32_t address,uint32_t size) {
    if(!size)return NO_ALLOCATION;
    if(ctr_memory_check_range(&g->memory,address,size,3)!=CTR_MEM_OK)return NO_ALLOCATION;
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i) {
        const ctr_guest_allocation *a=&g->allocations[i];
        if(!a->storage || address<a->base || (uint64_t)address+size>(uint64_t)a->base+a->size)continue;
        uint64_t cursor=address,end=cursor+size;
        while(cursor<end) {
            bool found=false;
            for(size_t j=0;j<g->memory.count;++j) {
                const ctr_mapping *map=&g->memory.maps[j];
                if(cursor>=map->base && cursor<(uint64_t)map->base+map->size &&
                    map->storage+(cursor-map->base)==a->storage+(cursor-a->base)) {
                    uint64_t next=(uint64_t)map->base+map->size;cursor=next<end?next:end;found=true;break;
                }
            }
            if(!found)return NO_ALLOCATION;
        }
        return i;
    }
    return NO_ALLOCATION;
}
static uint32_t segment_size(const module *m,unsigned type) {
    for(unsigned i=0;i<field(m,SEGMENT_COUNT);++i)if(m->segments[i].type==type)return m->segments[i].size;
    return 0;
}
static bool still_mapped(const ctr_guest *g,const module *m) {
    uint64_t at=m->image->address,end=at+m->image->size;
    while(at<end) {
        bool found=false;
        for(size_t i=0;i<g->memory.count;++i) {
            const ctr_mapping *map=&g->memory.maps[i];
            if(at>=map->base && at<(uint64_t)map->base+map->size &&
                map->storage+(at-map->base)==m->file+(at-m->image->address)) {
                uint64_t next=(uint64_t)map->base+map->size;at=next<end?next:end;found=true;break;
            }
        }
        if(!found)return false;
    }
    return true;
}
static bool file_exclusive(const ctr_guest *g,const module *m) {
    uintptr_t first=(uintptr_t)m->file,last=first+m->image->size;
    for(size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *map=&g->memory.maps[i];uintptr_t at=(uintptr_t)map->storage;
        if(at<last && first<at+map->size && (map->base<m->image->address ||
            (uint64_t)map->base+map->size>(uint64_t)m->image->address+m->image->size))return false;
    }
    return true;
}
static bool stage_current(ctr_guest *g,module *m,staging *s) {
    memset(s,0,sizeof(*s));s->file=malloc(m->image->size);
    uint32_t data=segment_size(m,2),bss=segment_size(m,3);
    s->data=data?malloc(data):NULL;s->bss=bss?malloc(bss):NULL;
    if(!s->file || (data && !s->data) || (bss && !s->bss))return false;
    memcpy(s->file,m->file,m->image->size);
    return (!data || ctr_memory_read_bytes(&g->memory,m->image->data_address,s->data,data)==CTR_MEM_OK) &&
        (!bss || ctr_memory_read_bytes(&g->memory,m->image->bss_address,s->bss,bss)==CTR_MEM_OK);
}
static void rebase_metadata(module *m,staging *s) {
    uint32_t base=m->image->address;
    for(unsigned f=CODE;f<FIELDS;f+=2)if(field(m,f))write32(s->file+0x80+f*4,base+field(m,f));
    if(field(m,NAME))write32(s->file+0x80+NAME*4,base+field(m,NAME));
    write32(s->file+0x80+FIXED*4,m->image->size);
    for(unsigned i=0;i<field(m,SEGMENT_COUNT);++i)
        write32(s->file+field(m,SEGMENTS)+i*12,m->segments[i].address);
    const unsigned entries[][2]={{EXPORTS,EXPORT_COUNT},{IMPORTS,IMPORT_COUNT},{INDEX_IMPORTS,INDEX_IMPORT_COUNT},{ANON_IMPORTS,ANON_IMPORT_COUNT}};
    for(unsigned t=0;t<4;++t)for(unsigned i=0;i<field(m,entries[t][1]);++i) {
        uint32_t at=field(m,entries[t][0])+i*8;
        if(t<2)write32(s->file+at,base+read32(m->image->bytes+at));
        if(t>0)write32(s->file+at+4,base+read32(m->image->bytes+at+4));
    }
    for(unsigned i=0;i<field(m,IMPORT_MODULE_COUNT);++i)for(unsigned off=0;off<=12;off+=off?8:4) {
        uint32_t at=field(m,IMPORT_MODULES)+i*20+off,value=read32(m->image->bytes+at);
        if(value)write32(s->file+at,base+value);
    }
}
static ctr_mem_result map_file(ctr_memory *memory,const module *m,uint8_t *file,bool executable) {
    ctr_mem_result result=ctr_memory_map(memory,m->image->address,m->image->size,CTR_MEM_READ,file,m->image->size);
    if(result!=CTR_MEM_OK || m->image->is_static || !executable)return result;
    uint32_t begin=field(m,CODE)&~4095u,end=(field(m,CODE)+field(m,CODE_SIZE)+4095u)&~4095u;
    return ctr_memory_change_range(memory,m->image->address+begin,end-begin,CTR_MEM_READ|CTR_MEM_EXEC,false);
}
static void accepted(ctr_guest *g) {
    g->cpu.exclusive_valid=false;g->modules->generation=g->memory.code_generation;
    g->program.code_generation=g->memory.code_generation;
}
/* Rebuild both bounded public registration lists from owned records. The CRS
 * holds auto/manual heads; each head's previous word identifies its tail. */
static void registration_lists(ctr_guest *g) {
    loader *l=g->modules;uint32_t words[CTR_MODULE_MAX_IMAGES][2]={{0}};size_t root=l->count;
    for(size_t i=0;i<l->count;++i)if(l->loaded[i].file && l->images[i].is_static)root=i;
    if(root==l->count)return;
    for(unsigned automatic=0;automatic<2;++automatic) {
        size_t first=l->count,last=l->count;
        for(size_t i=0;i<l->count;++i) {
            module *m=&l->loaded[i];if(!m->file || m->image->is_static || m->automatic!=(automatic!=0))continue;
            if(first==l->count)first=i;
            if(last!=l->count) { words[last][0]=m->image->address;words[i][1]=l->images[last].address; }
            last=i;
        }
        if(first!=l->count) { words[root][automatic?0:1]=l->images[first].address;words[first][1]=l->images[last].address; }
    }
    for(size_t i=0;i<l->count;++i)if(l->loaded[i].file) {
        uint8_t *p=l->loaded[i].file+0x80+NEXT*4;
        if(read32(p)!=words[i][0] || read32(p+4)!=words[i][1]) {
            write32(p,words[i][0]);write32(p+4,words[i][1]);(void)ctr_memory_backing_changed(&g->memory,p,8);
        }
    }
}
static ctr_module_result record(ctr_guest *g,ctr_module_result result) {
    if(g->modules) { g->modules->last=result;if(result)++g->modules->failures; }
    return result;
}
static bool data_aliases_executable(const ctr_memory *memory,const uint8_t *storage,uint32_t size) {
    uintptr_t at=(uintptr_t)storage;
    for(size_t i=0;size && i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];uintptr_t other=(uintptr_t)map->storage;
        if((map->permissions&CTR_MEM_EXEC) && at<other+map->size && other<at+size)return true;
    }
    return false;
}
ctr_module_result ctr_modules_load(ctr_guest *g,uint32_t source,uint32_t address,uint32_t size,
    uint32_t data,uint32_t data_size,uint32_t bss,uint32_t bss_size,bool link,bool is_static) {
    loader *l=g->modules;module m;staging s={0};ctr_module_result result=CTR_MODULE_STATE;
    if(!l)return result;
    if(!ctr_modules_generation_current(g))return record(g,CTR_MODULE_INVALIDATED);
    size_t slot=l->count;bool initialized=false;
    for(size_t i=0;i<l->count;++i) {
        if(l->loaded[i].file && l->images[i].is_static)initialized=true;
        if(l->images[i].address==address && l->images[i].is_static==is_static)slot=i;
    }
    if(slot==l->count)return record(g,CTR_MODULE_IDENTITY);
    if(l->loaded[slot].file || (is_static?initialized:!initialized))return record(g,CTR_MODULE_STATE);
    const ctr_module_image *image=&l->images[slot];
    if(size!=image->size || data!=image->data_address || bss!=image->bss_address ||
        (source&4095u) || (data&3u) || (bss&3u))return record(g,CTR_MODULE_IDENTITY);
    result=parse(&m,image);if(result)return record(g,result);
    s.file=malloc(size);if(!s.file)return record(g,CTR_MODULE_CAPACITY);
    if(ctr_memory_check_range(&g->memory,source,size,3)!=CTR_MEM_OK ||
        ctr_memory_read_bytes(&g->memory,source,s.file,size)!=CTR_MEM_OK) { result=CTR_MODULE_MEMORY;goto done; }
    if(memcmp(s.file,image->bytes,size)) { result=CTR_MODULE_IDENTITY;goto done; }
    uint64_t bytes=size+ctr_modules_bytes(g);
    for(unsigned i=0;i<CTR_GUEST_STORAGE_COUNT;++i)bytes+=g->storage_size[i];
    for(unsigned i=0;i<CTR_GUEST_MAX_ALLOCATIONS;++i)if(g->allocations[i].storage)bytes+=g->allocations[i].size;
    for(unsigned i=0;i<8;++i)if(g->shared[i].owned)bytes+=g->shared[i].size;
    if(bytes>CTR_GUEST_MAX_BYTES) { result=CTR_MODULE_CAPACITY;goto done; }
    if(is_static) {
        if(data_size || bss_size || data || bss) { result=CTR_MODULE_UNSUPPORTED;goto done; }
        for(unsigned i=0;i<field(&m,SEGMENT_COUNT);++i) {
            const segment *seg=&m.segments[i];unsigned rights=seg->type==0?5u:seg->type<2?1u:3u;
            if(seg->size && ctr_memory_check_range(&g->memory,seg->address,seg->size,rights)!=CTR_MEM_OK) {
                result=CTR_MODULE_MEMORY;goto done;
            }
        }
    } else {
        uint32_t needed[]={segment_size(&m,2),segment_size(&m,3)},addresses[]={data,bss},sizes[]={data_size,bss_size};
        if(overlaps(data,needed[0],bss,needed[1])) { result=CTR_MODULE_MEMORY;goto done; }
        for(unsigned i=0;i<2;++i) {
            if(!needed[i]) { if(addresses[i] || sizes[i]) { result=CTR_MODULE_MEMORY;goto done; }continue; }
            m.allocations[i]=allocation_for(g,addresses[i],sizes[i]);
            if(sizes[i]<needed[i] || m.allocations[i]==NO_ALLOCATION) { result=CTR_MODULE_MEMORY;goto done; }
            const ctr_guest_allocation *a=&g->allocations[m.allocations[i]];
            if(data_aliases_executable(&g->memory,a->storage+(addresses[i]-a->base),needed[i])) { result=CTR_MODULE_MEMORY;goto done; }
            for(size_t j=0;j<l->count;++j)if(l->loaded[j].file)for(unsigned k=0;k<field(&l->loaded[j],SEGMENT_COUNT);++k) {
                const segment *other=&l->loaded[j].segments[k];
                if(other->type>=2 && overlaps(addresses[i],needed[i],other->address,other->size)) { result=CTR_MODULE_BUSY;goto done; }
            }
        }
        s.data=needed[0]?malloc(needed[0]):NULL;s.bss=needed[1]?calloc(1,needed[1]):NULL;
        if((needed[0] && !s.data) || (needed[1] && !s.bss)) { result=CTR_MODULE_CAPACITY;goto done; }
        if(needed[0])memcpy(s.data,image->bytes+field(&m,DATA),needed[0]);
        result=apply_links(l,&m,&s,link);if(result)goto done;
        if(link && memcmp(s.file+field(&m,CODE),image->code,image->code_size)) { result=CTR_MODULE_IDENTITY;goto done; }
    }
    rebase_metadata(&m,&s);
    for(unsigned i=0;i<field(&m,EXTERNAL_COUNT);++i)s.file[field(&m,EXTERNAL)+i*12+6]=link?1:0;
    ctr_memory next=g->memory;
    if(map_file(&next,&m,s.file,link)!=CTR_MEM_OK) { result=CTR_MODULE_MEMORY;goto done; }
    ctr_memory_write_range outputs[]={{data,s.data,is_static?0:segment_size(&m,2)},{bss,s.bss,is_static?0:segment_size(&m,3)}};
    if(ctr_memory_write_ranges(&next,outputs,2)!=CTR_MEM_OK) { result=CTR_MODULE_MEMORY;goto done; }
    m.file=s.file;m.source=source;m.linked=is_static || link;m.automatic=link;m.dependencies=s.dependencies;
    l->loaded[slot]=m;g->memory=next;s.file=NULL;
    for(unsigned i=0;i<2;++i)if(m.allocations[i]<NO_ALLOCATION)++g->allocations[m.allocations[i]].pins;
    registration_lists(g);accepted(g);++l->loads;result=CTR_MODULE_OK;
done:
    release_stage(&s);return record(g,result);
}

static bool code_contains(const module *m,uint32_t address) {
    return !m->image->is_static && address>=m->image->address+field(m,CODE) &&
        (uint64_t)address<(uint64_t)m->image->address+field(m,CODE)+field(m,CODE_SIZE);
}
static bool busy(const ctr_guest *g,unsigned slot) {
    const loader *l=g->modules;const module *m=&l->loaded[slot];
    for(size_t i=0;i<l->count;++i)if(l->loaded[i].file && (l->loaded[i].dependencies&(1u<<slot)))return true;
    if(code_contains(m,g->cpu.r[15]))return true;
    for(unsigned i=0;i<CTR_GUEST_MAX_THREADS;++i)if(i!=g->current_thread && g->threads[i].active && !g->threads[i].finished &&
        code_contains(m,g->threads[i].r[15]))return true;
    return false;
}
ctr_module_result ctr_modules_link(ctr_guest *g,uint32_t address,bool link) {
    loader *l=g->modules;if(!l)return CTR_MODULE_STATE;
    if(!ctr_modules_generation_current(g))return record(g,CTR_MODULE_INVALIDATED);
    size_t slot=0;while(slot<l->count && (!l->loaded[slot].file || l->images[slot].address!=address))++slot;
    if(slot==l->count || l->images[slot].is_static)return record(g,CTR_MODULE_STATE);
    module *m=&l->loaded[slot];if(m->linked==link)return record(g,CTR_MODULE_OK);
    if(busy(g,(unsigned)slot))return record(g,CTR_MODULE_BUSY);
    if(!still_mapped(g,m))return record(g,CTR_MODULE_MEMORY);
    if(!file_exclusive(g,m))return record(g,CTR_MODULE_BUSY);
    staging s={0};ctr_module_result result=CTR_MODULE_MEMORY;
    if(!stage_current(g,m,&s)) { release_stage(&s);return record(g,CTR_MODULE_MEMORY); }
    result=apply_links(l,m,&s,link);if(result)goto done;
    if(link && memcmp(s.file+field(m,CODE),m->image->code,m->image->code_size)) { result=CTR_MODULE_IDENTITY;goto done; }
    for(unsigned i=0;i<field(m,EXTERNAL_COUNT);++i)s.file[field(m,EXTERNAL)+i*12+6]=link?1:0;
    ctr_memory next=g->memory;
    if(ctr_memory_change_range(&next,address,m->image->size,0,true)!=CTR_MEM_OK || map_file(&next,m,s.file,link)!=CTR_MEM_OK) {
        result=CTR_MODULE_MEMORY;goto done;
    }
    ctr_memory_write_range outputs[]={{m->image->data_address,s.data,segment_size(m,2)},{m->image->bss_address,s.bss,segment_size(m,3)}};
    if(ctr_memory_write_ranges(&next,outputs,2)!=CTR_MEM_OK) { result=CTR_MODULE_MEMORY;goto done; }
    uint8_t *previous=m->file;m->file=s.file;s.file=NULL;m->linked=link;m->dependencies=s.dependencies;
    g->memory=next;free(previous);registration_lists(g);accepted(g);++l->links;result=CTR_MODULE_OK;
done:
    release_stage(&s);return record(g,result);
}
ctr_module_result ctr_modules_unload(ctr_guest *g,uint32_t address,uint32_t source,bool is_static) {
    loader *l=g->modules;if(!l)return CTR_MODULE_STATE;
    if(!ctr_modules_generation_current(g))return record(g,CTR_MODULE_INVALIDATED);
    size_t slot=0;while(slot<l->count && (!l->loaded[slot].file || l->images[slot].address!=address))++slot;
    if(slot==l->count)return record(g,CTR_MODULE_STATE);
    module *m=&l->loaded[slot];
    if(m->image->is_static!=is_static || source!=m->source)return record(g,CTR_MODULE_IDENTITY);
    if(busy(g,(unsigned)slot))return record(g,CTR_MODULE_BUSY);
    if(is_static)for(size_t i=0;i<l->count;++i)if(i!=slot && l->loaded[i].file)return record(g,CTR_MODULE_BUSY);
    if(!still_mapped(g,m))return record(g,CTR_MODULE_MEMORY);
    if(!file_exclusive(g,m))return record(g,CTR_MODULE_BUSY);
    ctr_memory next=g->memory;
    if(ctr_memory_change_range(&next,address,m->image->size,0,true)!=CTR_MEM_OK)return record(g,CTR_MODULE_MEMORY);
    g->memory=next;
    for(unsigned i=0;i<2;++i)if(m->allocations[i]<NO_ALLOCATION)--g->allocations[m->allocations[i]].pins;
    free(m->file);memset(m,0,sizeof(*m));registration_lists(g);accepted(g);++l->unloads;return record(g,CTR_MODULE_OK);
}
ctr_module_result ctr_modules_lookup(ctr_guest *g,uint32_t address,const char *name,uint32_t *value) {
    loader *l=g->modules;if(!l || !name || !value)return CTR_MODULE_STATE;
    if(!ctr_modules_generation_current(g))return CTR_MODULE_INVALIDATED;
    for(size_t i=0;i<l->count;++i) {
        const module *m=&l->loaded[i];
        if(m->file && m->linked && m->image->address==address)
            return export_named(m,name,value)?CTR_MODULE_OK:CTR_MODULE_SYMBOL;
    }
    return CTR_MODULE_STATE;
}
bool ctr_modules_has_code(const ctr_guest *g,uint32_t address) {
    const loader *l=g->modules;if(!l)return false;
    for(size_t i=0;i<l->count;++i)if(l->loaded[i].file && code_contains(&l->loaded[i],address))return true;
    return false;
}
bool ctr_modules_mapping(const ctr_guest *g,const ctr_mapping *map) {
    const loader *l=g->modules;if(!l)return false;
    for(size_t i=0;i<l->count;++i) {
        const module *m=&l->loaded[i];
        if(m->file && map->base>=m->image->address &&
            (uint64_t)map->base+map->size<=(uint64_t)m->image->address+m->image->size &&
            map->storage==m->file+(map->base-m->image->address))return true;
    }
    return false;
}
ctr_cpu_result ctr_modules_dispatch(ctr_guest *g,uint64_t budget) {
    loader *l=g->modules;if(!l)return ctr_cpu_dispatch(&g->cpu,&g->program,budget,false);
    uint64_t start=g->cpu.instructions;
    while(g->cpu.instructions-start<budget) {
        if(!ctr_modules_generation_current(g))return ctr_cpu_stop(&g->cpu,CTR_CPU_CODE_INVALIDATED,g->cpu.r[15]);
        ctr_program program=g->program;uint32_t pc=g->cpu.r[15];
        unsigned width=(g->cpu.cpsr&CTR_FLAG_T)?2u:4u;
        ctr_mem_result checked=pc%width?CTR_MEM_ALIGNMENT:ctr_memory_check_range(&g->memory,pc,width,CTR_MEM_EXEC);
        if(checked!=CTR_MEM_OK) { g->cpu.memory_fault=checked;return ctr_cpu_stop(&g->cpu,CTR_CPU_MEMORY_FAULT,pc); }
        for(size_t i=0;i<l->count;++i) {
            const module *m=&l->loaded[i];if(!m->file || !code_contains(m,pc))continue;
            if(!m->linked)return ctr_cpu_stop(&g->cpu,CTR_CPU_MISSING_BLOCK,pc);
            program=(ctr_program){m->image->blocks,m->image->block_count,m->image->identity,l->generation};break;
        }
        ctr_cpu_result result=ctr_cpu_dispatch(&g->cpu,&program,1,false);
        if(result!=CTR_CPU_LIMIT)return result;
    }
    return CTR_CPU_LIMIT;
}
ctr_cpu_result ctr_modules_interpret(ctr_guest *g,uint64_t budget) {
    if(!g->modules)return ctr_cpu_run(&g->cpu,budget);
    uint64_t start=g->cpu.instructions;
    while(g->cpu.instructions-start<budget) {
        g->cpu.module_hash=g->program.module_hash;
        for(size_t i=0;i<g->modules->count;++i) {
            const module *m=&g->modules->loaded[i];
            if(m->file && code_contains(m,g->cpu.r[15]))g->cpu.module_hash=m->image->identity;
        }
        ctr_cpu_result result=ctr_cpu_step(&g->cpu);if(result!=CTR_CPU_OK)return result;
    }
    return CTR_CPU_LIMIT;
}
static uint32_t result_code(ctr_module_result result) {
    static const uint32_t codes[]={0,0xd8a12c11u,0xd8a12c22u,0xd8a12c08u,0xe0e12c1fu,
        0xd8602c07u,0xd9612ff8u,0xd8a12c21u,0xd8a12c0du,0xd8a12c08u};
    return (unsigned)result<sizeof(codes)/sizeof(*codes)?codes[result]:0xd8a12c22u;
}
bool ctr_modules_request(ctr_guest *g,unsigned session,const uint32_t *w,uint32_t command_base,uint32_t *out,unsigned *count) {
    (void)session;
    unsigned command=w[0]>>16,normal=(w[0]>>6)&63u;
    if(!g->modules || normal>61u || (w[0]&63u)!=2u || w[1+normal] || w[2+normal]!=0xffff8001u)return false;
    ctr_module_result result=CTR_MODULE_UNSUPPORTED;*count=2;out[0]=(command<<16)|0x40u;
    if(command==1 && normal==3) {
        if(overlaps(w[1],w[2],command_base,256))result=CTR_MODULE_MEMORY;
        else result=ctr_modules_load(g,w[1],w[3],w[2],0,0,0,0,true,true);
    } else if((command==4 || command==9) && normal==11) {
        *count=3;out[0]=(command<<16)|0x80u;out[2]=0;
        if(w[5] || w[9]>1 || w[10] || w[11])return false; /* fixed/trusted package profile, no CRR bypass */
        if(overlaps(w[1],w[3],command_base,256))result=CTR_MODULE_MEMORY;
        else result=ctr_modules_load(g,w[1],w[2],w[3],w[4],w[6],w[7],w[8],w[9]!=0,false);
        if(!result)out[2]=w[3];
    } else if(command==5 && normal==3) {
        if(w[2])return false;
        result=ctr_modules_unload(g,w[1],w[3],false);
    } else if((command==6 || command==7) && normal==1)result=ctr_modules_link(g,w[1],command==6);
    else if(command==8 && normal==1) {
        for(size_t i=0;i<g->modules->count;++i)if(g->modules->images[i].is_static && g->modules->loaded[i].file) {
            result=ctr_modules_unload(g,g->modules->images[i].address,w[1],true);break;
        }
    } else return false;
    out[1]=result_code(result);
    if(command==1 && result==CTR_MODULE_STATE)for(size_t i=0;i<g->modules->count;++i)
        if(g->modules->loaded[i].file && g->modules->images[i].is_static)out[1]=0xd9612ff9u;
    return true;
}
const char *ctr_module_result_name(ctr_module_result result) {
    static const char *const names[]={"ok","format","unsupported","identity","memory","capacity","state","symbol","busy","invalidated"};
    return (unsigned)result<sizeof(names)/sizeof(*names)?names[result]:"invalid";
}
