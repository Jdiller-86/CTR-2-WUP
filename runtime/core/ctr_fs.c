/* Original bounded virtual filesystem; interface facts from pinned libctru. */
#include "ctr_fs.h"
#include "ctr_guest.h"
#include <stdlib.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n;++i) { crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u))); }
    return ~crc;
}
/* Root files only. Reject invalid UTF-8, traversal, host-device characters and
   embedded separators. Non-ASCII names preserve their exact Unicode spelling. */
static bool path_ok(const char *s) {
    size_t n=strlen(s);if(n<2 || n>=CTR_FS_PATH_BYTES || s[0]!='/' || !strcmp(s,"/.") || !strcmp(s,"/.."))return false;
    for(size_t i=1;i<n;) {
        unsigned c=(unsigned char)s[i++],extra=0;uint32_t cp=c,min=0;
        if(c>=0xc2 && c<=0xdf) { extra=1;cp=c&31u;min=0x80; }
        else if(c>=0xe0 && c<=0xef) { extra=2;cp=c&15u;min=0x800; }
        else if(c>=0xf0 && c<=0xf4) { extra=3;cp=c&7u;min=0x10000; }
        else if(c>=0x80)return false;
        if(i+extra>n)return false;
        for(unsigned j=0;j<extra;++j) { c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63u); }
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) || cp<0x20 || cp==0x7f || cp=='/' || cp=='\\' || cp==':')return false;
    }
    return true;
}
static unsigned find(const ctr_fs_volume *v,const char *path) {
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].active && !strcmp(v->files[i].path,path))return i;
    return CTR_FS_MAX_FILES;
}
static uint32_t used(const ctr_fs_volume *v) {
    uint32_t n=0;for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].active)n+=v->files[i].size;return n;
}
static uint32_t resize(ctr_fs_volume *v,unsigned i,uint32_t size) {
    ctr_fs_file *f=&v->files[i];
    if(size>CTR_FS_MAX_FILE_BYTES || used(v)-f->size+size>CTR_FS_MAX_VOLUME_BYTES)return CTR_FS_FULL;
    if(size!=f->size) {
        uint8_t *p=size?calloc(1,size):NULL;if(size && !p)return CTR_FS_FULL;
        if(size && f->size)memcpy(p,f->bytes,size<f->size?size:f->size);
        free(f->bytes);f->bytes=p;f->size=size;v->dirty=true;
    }
    return 0;
}
bool ctr_fs_volume_init(ctr_fs_volume *v,uint32_t id,bool writable) {
    if(!v || (id!=4 && id!=9))return false;
    memset(v,0,sizeof(*v));v->archive_id=id;v->writable=writable;return true;
}
bool ctr_fs_volume_close(ctr_fs_volume *v) {
    if(!v || v->mounts)return false;
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].references)return false;
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)free(v->files[i].bytes);
    memset(v,0,sizeof(*v));return true;
}
uint32_t ctr_fs_seed(ctr_fs_volume *v,const char *path,const void *bytes,uint32_t size) {
    if(!v || (v->archive_id!=4 && v->archive_id!=9) || !path || !path_ok(path) || (size && !bytes))return CTR_FS_INVALID;
    if(find(v,path)!=CTR_FS_MAX_FILES)return CTR_FS_EXISTS;
    if(size>CTR_FS_MAX_FILE_BYTES || used(v)+size>CTR_FS_MAX_VOLUME_BYTES)return CTR_FS_FULL;
    unsigned i;for(i=0;i<CTR_FS_MAX_FILES && v->files[i].active;++i){}
    if(i==CTR_FS_MAX_FILES)return CTR_FS_FULL;
    uint8_t *p=size?malloc(size):NULL;if(size && !p)return CTR_FS_FULL;
    if(size)memcpy(p,bytes,size);
    v->files[i]=(ctr_fs_file){.bytes=p,.size=size,.active=true};strcpy(v->files[i].path,path);v->dirty=true;return 0;
}
bool ctr_fs_mount(ctr_guest *g,ctr_fs_volume *v) {
    if(!g || !v || (v->archive_id!=4 && v->archive_id!=9) || g->state!=CTR_GUEST_READY || g->svc_count || g->cpu.instructions)return false;
    unsigned slot=v->archive_id==4?0:1;if(g->fs_volumes[slot])return false;
    g->fs_volumes[slot]=v;++v->mounts;return true;
}
bool ctr_fs_romfs_init(ctr_fs_romfs *view,uint64_t size,
                       bool (*read)(void *,uint64_t,void *,uint32_t),void *context) {
    if(!view || !read || view->read || view->mounts || view->references)return false;
    memset(view,0,sizeof(*view));view->size=size;view->read=read;view->context=context;return true;
}
bool ctr_fs_romfs_close(ctr_fs_romfs *view) {
    if(!view || view->mounts || view->references)return false;
    memset(view,0,sizeof(*view));return true;
}
bool ctr_fs_mount_romfs(ctr_guest *g,ctr_fs_romfs *view) {
    if(!g || !view || !view->read || g->fs_romfs || g->state!=CTR_GUEST_READY || g->svc_count || g->cpu.instructions)return false;
    g->fs_romfs=view;++view->mounts;return true;
}
void ctr_fs_session_closed(ctr_guest *g,unsigned slot) {
    ctr_guest_session *s=&g->sessions[slot];
    if(s->service==CTR_SERVICE_FILE && !s->fs_closed) {
        if(s->fs_romfs) {
            if(g->fs_romfs && g->fs_romfs->references)--g->fs_romfs->references;
        } else {
            ctr_fs_volume *v=g->fs_volumes[s->fs_volume];
            if(v && v->files[s->fs_file].references)--v->files[s->fs_file].references;
        }
        s->fs_closed=true;
    }
    if(s->service==CTR_SERVICE_FS)
        for(unsigned i=0;i<8;++i)if(g->fs_archives[i].active && g->fs_archives[i].owner==slot)g->fs_archives[i].active=false;
}
static bool memory(ctr_guest *g,ctr_mem_result result,uint32_t at) {
    if(result==CTR_MEM_OK)return true;
    g->state=CTR_GUEST_FAULTED;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=at;g->cpu.memory_fault=result;
    ctr_cpu_stop(&g->cpu,CTR_CPU_MEMORY_FAULT,at);return false;
}
static bool overlap(uintptr_t a,uint64_t n,uintptr_t b,uint64_t m) {
    return a<=b?(uint64_t)(b-a)<n:(uint64_t)(a-b)<m;
}
static bool aliases(ctr_guest *g,uint32_t at,uint32_t size,uint32_t base) {
    for(size_t i=0;i<g->memory.count;++i) {
        const ctr_mapping *a=&g->memory.maps[i];uint64_t lo=at>a->base?at:a->base,hi=(uint64_t)at+size;
        if(hi>(uint64_t)a->base+a->size)hi=(uint64_t)a->base+a->size;
        if(lo>=hi)continue;
        for(size_t j=0;j<g->memory.count;++j) {
            const ctr_mapping *b=&g->memory.maps[j];uint64_t bl=base>b->base?base:b->base,bh=(uint64_t)base+256;
            if(bh>(uint64_t)b->base+b->size)bh=(uint64_t)b->base+b->size;
            if(bl<bh && overlap((uintptr_t)a->storage+(uintptr_t)(lo-a->base),hi-lo,(uintptr_t)b->storage+(uintptr_t)(bl-b->base),bh-bl))return true;
        }
    }
    return false;
}
bool ctr_fs_buffer(ctr_guest *g,uint32_t at,uint32_t size,unsigned permissions,uint32_t base) {
    if(!memory(g,ctr_memory_check_range(&g->memory,at,size,permissions),at))return false;
    return !aliases(g,at,size,base);
}
static bool copy_in(ctr_guest *g,uint32_t at,uint8_t *p,uint32_t n) {
    for(uint32_t i=0;i<n;++i) { uint32_t byte;if(!memory(g,ctr_memory_read(&g->memory,at+i,1,CTR_MEM_READ,&byte),at+i))return false;p[i]=(uint8_t)byte; }return true;
}
static bool copy_out(ctr_guest *g,uint32_t at,const uint8_t *p,uint32_t n) {
    for(uint32_t i=0;i<n;++i)
        if(!memory(g,ctr_memory_write(&g->memory,at+i,1,p[i]),at+i))return false;
    return true;
}
static bool decode_path(ctr_guest *g,uint32_t type,uint32_t size,uint32_t descriptor,uint32_t at,uint32_t base,char *path,bool empty) {
    if(size>512 || descriptor!=((size<<14)|2u) || !ctr_fs_buffer(g,at,size,CTR_MEM_READ,base))return false;
    uint8_t raw[512];if(!copy_in(g,at,raw,size))return false;
    if(empty)return type==1 && size==1 && !raw[0];
    unsigned n=0;
    if(type==3) {
        if(size<2 || size>CTR_FS_PATH_BYTES || raw[size-1] || memchr(raw,0,size-1))return false;
        for(unsigned i=0;i<size-1;++i)if(raw[i]>=128)return false;
        memcpy(path,raw,size);return path_ok(path);
    }
    if(type!=4 || size<4 || (size&1) || raw[size-1] || raw[size-2])return false;
    for(unsigned i=0;i<size-2;i+=2) {
        uint32_t cp=(uint32_t)raw[i]|(uint32_t)raw[i+1]<<8;if(!cp)return false;
        if(cp>=0xd800 && cp<=0xdbff) {
            if(i+4>=size)return false;
            uint32_t low=(uint32_t)raw[i+2]|(uint32_t)raw[i+3]<<8;if(low<0xdc00 || low>0xdfff)return false;
            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);i+=2;
        } else if(cp>=0xdc00 && cp<=0xdfff)return false;
        unsigned width=cp<128?1:cp<0x800?2:cp<0x10000?3:4;
        if(n+width>=CTR_FS_PATH_BYTES)return false;
        if(width==1)path[n++]=(char)cp;
        else { path[n++]=(char)((width==2?0xc0:width==3?0xe0:0xf0)|(cp>>(6*(width-1))));for(unsigned b=width-1;b;--b)path[n++]=(char)(0x80|((cp>>(6*(b-1)))&63)); }
    }
    path[n]=0;return path_ok(path);
}
static unsigned archive(ctr_guest *g,unsigned owner,uint32_t lo,uint32_t hi) {
    unsigned slot=(lo&255u);if(hi || !slot || slot>8)return 8;
    --slot;return g->fs_archives[slot].active && g->fs_archives[slot].owner==owner && g->fs_archives[slot].generation==(lo>>8)?slot:8;
}
static uint32_t commit(ctr_fs_volume *v) {
    if(!v->writable)return 0;
    if(!v->persist || !v->persist(v->persist_context,v))return CTR_FS_DENIED;
    v->dirty=false;++v->commits;return 0;
}
static bool romfs_file_request(ctr_guest *g,unsigned slot,const uint32_t *w,uint32_t base,
                               uint32_t *o,unsigned *count) {
    ctr_guest_session *session=&g->sessions[slot];ctr_fs_romfs *view=g->fs_romfs;
    if(!view || !view->read) { o[1]=CTR_FS_INVALID;return true; }
    uint64_t offset=(uint64_t)w[1]|(uint64_t)w[2]<<32;
    switch(w[0]) {
        case 0x080200c2: case 0x08030102: {
            bool write=w[0]==0x08030102;unsigned d=write?5:4;uint32_t size=w[3];
            if(size>CTR_FS_MAX_FILE_BYTES || w[d]!=(size<<4|(write?0xau:0xcu)) || (write && (w[4]&~1u)))return false;
            if(!ctr_fs_buffer(g,w[d+1],size,write?CTR_MEM_READ:CTR_MEM_WRITE,base))return false;
            o[0]=(w[0]&0xffff0000u)|0x82u;*count=5;o[3]=w[d];o[4]=w[d+1];
            if(write) { o[1]=CTR_FS_DENIED;return true; }
            uint64_t available=offset>=view->size?0:view->size-offset;
            uint32_t n=available<size?(uint32_t)available:size;
            uint8_t *bytes=n?malloc(n):NULL;
            if(n && !bytes) { o[1]=CTR_FS_FULL;return true; }
            if(n && !view->read(view->context,offset,bytes,n)) { free(bytes);o[1]=CTR_FS_DENIED;return true; }
            bool ok=!n || copy_out(g,w[5],bytes,n);free(bytes);
            if(!ok)return false;
            o[2]=n;return true;
        }
        case 0x08040000:o[0]=0x080400c0;o[2]=(uint32_t)view->size;o[3]=(uint32_t)(view->size>>32);*count=4;return true;
        case 0x08050080:o[1]=CTR_FS_DENIED;return true;
        case 0x08080000:ctr_fs_session_closed(g,slot);return true;
        case 0x08090000:return session->fs_flags==1;
        default:return false;
    }
}
bool ctr_fs_request(ctr_guest *g,unsigned slot,const uint32_t *w,uint32_t base,uint32_t *o,unsigned *count) {
    ctr_guest_session *s=&g->sessions[slot];o[0]=(w[0]&0xffff0000u)|0x40u;*count=2;
    if(s->service==CTR_SERVICE_FILE) {
        if(s->fs_closed) { o[1]=CTR_FS_INVALID;return true; }
        if(s->fs_romfs)return romfs_file_request(g,slot,w,base,o,count);
        ctr_fs_volume *v=g->fs_volumes[s->fs_volume];ctr_fs_file *f=&v->files[s->fs_file];
        uint64_t offset=(uint64_t)w[1]|(uint64_t)w[2]<<32;
        switch(w[0]) {
            case 0x080200c2: case 0x08030102: {
                bool write=w[0]==0x08030102;unsigned d=write?5:4;uint32_t size=w[3];
                if(size>CTR_FS_MAX_FILE_BYTES || w[d]!=(size<<4|(write?0xau:0xcu)) || (write && (w[4]&~1u)))return false;
                if(!ctr_fs_buffer(g,w[d+1],size,write?CTR_MEM_READ:CTR_MEM_WRITE,base))return false;
                o[0]=(w[0]&0xffff0000u)|0x82u;*count=5;o[3]=w[d];o[4]=w[d+1];
                if(!(s->fs_flags&(write?2u:1u))) { o[1]=CTR_FS_DENIED;return true; }
                if(write) {
                    if(offset>CTR_FS_MAX_FILE_BYTES || size>CTR_FS_MAX_FILE_BYTES-offset) { o[1]=CTR_FS_FULL;return true; }
                    uint8_t *p=size?malloc(size):NULL;if(size && !p) { o[1]=CTR_FS_FULL;return true; }
                    if(!copy_in(g,w[d+1],p,size)) { free(p);return false; }
                    uint32_t end=(uint32_t)offset+size;
                    if(size && end>f->size)o[1]=resize(v,s->fs_file,end);
                    if(!o[1] && size) { memcpy(f->bytes+(size_t)offset,p,size);v->dirty=true; }
                    free(p);if(!o[1] && (w[4]&1))o[1]=commit(v);
                    if(!o[1])o[2]=size;
                } else {
                    uint32_t n=offset>=f->size?0:f->size-(uint32_t)offset;if(n>size)n=size;
                    if(n && !copy_out(g,w[5],f->bytes+(size_t)offset,n))return false;
                    o[2]=n;
                }
                return true;
            }
            case 0x08040000:o[0]=0x080400c0;o[2]=f->size;*count=4;return true;
            case 0x08050080:
                o[1]=!(s->fs_flags&2)?CTR_FS_DENIED:offset>CTR_FS_MAX_FILE_BYTES?CTR_FS_FULL:resize(v,s->fs_file,(uint32_t)offset);return true;
            case 0x08080000:ctr_fs_session_closed(g,slot);return true;
            case 0x08090000:o[1]=commit(v);return true;
            default:return false;
        }
    }
    if(w[0]==0x08010002 || w[0]==0x08610042) {
        if(w[w[0]==0x08010002?1:2]!=0x20)return false;
        s->registered=true;return true;
    }
    if(!s->registered) { o[1]=CTR_FS_INVALID;return true; }
    char path[CTR_FS_PATH_BYTES];
    if(w[0]==0x08030204) {
        /* Current-process RomFS: empty archive path in static slot 2 and a
           12-byte all-zero binary file path in slot 0, per pinned libctru. */
        if(w[1] || w[3]!=1 || w[4]!=1 || w[5]!=2 || w[6]!=12 || w[8] ||
           w[9]!=0x4802 || w[11]!=0x30002 || !(w[7]&3) || (w[7]&~7u))return false;
        if(!ctr_fs_buffer(g,w[10],1,CTR_MEM_READ,base) || !ctr_fs_buffer(g,w[12],12,CTR_MEM_READ,base))return false;
        uint8_t empty,raw[12];if(!copy_in(g,w[10],&empty,1) || !copy_in(g,w[12],raw,12))return false;
        if(empty)return false;
        for(unsigned i=0;i<12;++i)if(raw[i])return false;
        if(w[2]!=3 || !g->fs_romfs) { o[1]=CTR_FS_NOT_FOUND;return true; }
        if(w[7]!=1) { o[1]=CTR_FS_DENIED;return true; }
        uint32_t handle=ctr_guest_session_open(g,CTR_SERVICE_FILE);
        if(!handle) { o[1]=CTR_FS_FULL;return true; }
        ctr_guest_session *file=ctr_guest_session_for(g,handle);
        file->fs_romfs=true;file->fs_flags=1;file->fs_closed=false;++g->fs_romfs->references;
        o[0]=0x08030042;o[2]=0x10;o[3]=handle;*count=4;return true;
    }
    if(w[0]==0x080c00c2) {
        if(!decode_path(g,w[2],w[3],w[4],w[5],base,path,true))return false;
        if(w[1]!=4 && w[1]!=9) { o[1]=CTR_FS_NOT_FOUND;return true; }
        unsigned volume=w[1]==4?0:1;if(!g->fs_volumes[volume]) { o[1]=CTR_FS_NOT_FOUND;return true; }
        unsigned i;for(i=0;i<8 && g->fs_archives[i].active;++i){}
        if(i==8 || g->fs_generation==0xffffffu) { o[1]=CTR_FS_FULL;return true; }
        g->fs_archives[i].generation=++g->fs_generation;g->fs_archives[i].active=true;g->fs_archives[i].owner=slot;g->fs_archives[i].volume=volume;
        o[0]=0x080c00c0;o[2]=(g->fs_generation<<8)|(i+1);*count=4;return true;
    }
    bool file=w[0]==0x080201c2,create=w[0]==0x08080202,remove=w[0]==0x08040142;
    if(file || create || remove) {
        unsigned d=file?8:create?9:6;
        if(w[1] || !decode_path(g,w[4],w[5],w[d],w[d+1],base,path,false))return false;
        if((file && (!(w[6]&3) || (w[6]&~7u) || w[7])) || (create && w[6]))return false;
        unsigned a=archive(g,slot,w[2],w[3]);if(a==8) { o[1]=CTR_FS_INVALID;return true; }
        unsigned vi=g->fs_archives[a].volume;ctr_fs_volume *v=g->fs_volumes[vi];unsigned i=find(v,path);
        if((create || remove || (file && (w[6]&6))) && !v->writable) { o[1]=CTR_FS_DENIED;return true; }
        if(remove) {
            if(i==CTR_FS_MAX_FILES)o[1]=CTR_FS_NOT_FOUND;
            else if(v->files[i].references)o[1]=CTR_FS_BUSY;
            else { free(v->files[i].bytes);memset(&v->files[i],0,sizeof(v->files[i]));v->dirty=true; }
            return true;
        }
        if(create) {
            uint64_t size=(uint64_t)w[7]|(uint64_t)w[8]<<32;
            if(i!=CTR_FS_MAX_FILES) { o[1]=CTR_FS_EXISTS;return true; }
            if(size>CTR_FS_MAX_FILE_BYTES || used(v)+size>CTR_FS_MAX_VOLUME_BYTES) { o[1]=CTR_FS_FULL;return true; }
            uint8_t *zero=size?calloc(1,(size_t)size):NULL;if(size && !zero) { o[1]=CTR_FS_FULL;return true; }
            o[1]=ctr_fs_seed(v,path,zero,(uint32_t)size);free(zero);return true;
        }
        if(i==CTR_FS_MAX_FILES && !(w[6]&4)) { o[1]=CTR_FS_NOT_FOUND;return true; }
        /* Allocate the session first; exhaustion never leaves a created file. */
        uint32_t handle=ctr_guest_session_open(g,CTR_SERVICE_FILE);
        if(!handle) { o[1]=CTR_FS_FULL;return true; }
        ctr_guest_session *f=ctr_guest_session_for(g,handle);f->fs_closed=true;
        if(i==CTR_FS_MAX_FILES) { o[1]=ctr_fs_seed(v,path,NULL,0);i=find(v,path); }
        if(o[1]) { ctr_guest_handle_close(g,handle);return true; }
        f->fs_volume=vi;f->fs_file=i;f->fs_flags=w[6]&3;f->fs_closed=false;++v->files[i].references;
        o[0]=0x08020042;o[2]=0x10;o[3]=handle;*count=4;return true;
    }
    if(w[0]==0x080e0080 || w[0]==0x08120080 || w[0]==0x080d0144) {
        unsigned a=archive(g,slot,w[1],w[2]);if(a==8) { o[1]=CTR_FS_INVALID;return true; }
        ctr_fs_volume *v=g->fs_volumes[g->fs_archives[a].volume];
        if(w[0]==0x080e0080)g->fs_archives[a].active=false;
        else if(w[0]==0x08120080) { o[0]=0x081200c0;o[2]=CTR_FS_MAX_VOLUME_BYTES-used(v);*count=4; }
        else { if(w[3] || w[4] || w[5] || w[6]!=0xa || w[8]!=0xc)return false;o[1]=commit(v); }
        return true;
    }
    return false;
}
bool ctr_fs_export(const ctr_fs_volume *v,uint8_t **bytes,size_t *size) {
    if(!v || !bytes || !size || (v->archive_id!=4 && v->archive_id!=9))return false;
    size_t total=24;unsigned count=0;
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].active) { ++count;total+=8+strlen(v->files[i].path)+v->files[i].size; }
    uint8_t *p=calloc(1,total);if(!p)return false;
    memcpy(p,"CTRFS001",8);put32(p+8,v->archive_id);put32(p+12,count);put32(p+16,(uint32_t)total);size_t at=24;
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].active) {
        const ctr_fs_file *f=&v->files[i];size_t n=strlen(f->path);put32(p+at,(uint32_t)n);put32(p+at+4,f->size);at+=8;
        memcpy(p+at,f->path,n);at+=n;if(f->size)memcpy(p+at,f->bytes,f->size);at+=f->size;
    }
    put32(p+20,crc32(p+24,total-24)^crc32(p,20));*bytes=p;*size=total;return true;
}
bool ctr_fs_import(ctr_fs_volume *v,const uint8_t *p,size_t size) {
    if(!v || v->mounts || !p || size<24 || size>CTR_FS_MAX_VOLUME_BYTES+24+CTR_FS_MAX_FILES*(CTR_FS_PATH_BYTES+8) ||
       memcmp(p,"CTRFS001",8) || get32(p+8)!=v->archive_id || get32(p+12)>CTR_FS_MAX_FILES ||
       get32(p+16)!=size || get32(p+20)!=(crc32(p+24,size-24)^crc32(p,20)))return false;
    for(unsigned i=0;i<CTR_FS_MAX_FILES;++i)if(v->files[i].active || v->files[i].references)return false;
    ctr_fs_volume next;if(!ctr_fs_volume_init(&next,v->archive_id,v->writable))return false;
    size_t at=24;bool ok=true;
    for(unsigned i=0;i<get32(p+12) && ok;++i) {
        if(size-at<8) { ok=false;break; }
        uint32_t n=get32(p+at),length=get32(p+at+4);at+=8;
        if(!n || n>=CTR_FS_PATH_BYTES || n>size-at || length>size-at-n || memchr(p+at,0,n)) { ok=false;break; }
        char path[CTR_FS_PATH_BYTES];memcpy(path,p+at,n);path[n]=0;at+=n;
        ok=ctr_fs_seed(&next,path,p+at,length)==0;at+=length;
    }
    if(!ok || at!=size) { ctr_fs_volume_close(&next);return false; }
    next.persist=v->persist;next.persist_context=v->persist_context;next.dirty=false;*v=next;return true;
}
