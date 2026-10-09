/* Original CTR2WUP SRV/CFG transport from documented interface facts. */
#include "ctr_ipc.h"
#include "ctr_hid.h"
#include "ctr_gsp.h"
#include "ctr_fs.h"
#include "ctr_apt.h"
#include <string.h>

#define INVALID_HANDLE UINT32_C(0xd8e007f7)
#define NO_MEMORY UINT32_C(0xd86007f3)
/* Permanent / NotFound / SRV(25) / NotFound(1018). */
#define SERVICE_NOT_FOUND UINT32_C(0xd88067fa)

static void unsupported(ctr_guest *g,uint32_t detail) {
    ++g->ipc_failures;g->fault=CTR_CPU_UNSUPPORTED;g->fault_detail=detail;
    g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,CTR_CPU_UNSUPPORTED,detail);
}
static bool memory_ok(ctr_guest *g,ctr_mem_result result,uint32_t address) {
    if (result==CTR_MEM_OK) return true;
    ++g->ipc_failures;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=address;
    g->cpu.memory_fault=result;g->state=CTR_GUEST_FAULTED;
    ctr_cpu_stop(&g->cpu,CTR_CPU_MEMORY_FAULT,address);
    return false;
}
static bool reply(ctr_guest *g,uint32_t base,const uint32_t *words,unsigned count) {
    for (unsigned i=0;i<count;++i)
        if (!memory_ok(g,ctr_memory_write(&g->memory,base+i*4,4,words[i]),base+i*4)) return false;
    g->cpu.r[0]=0;return true;
}
static bool name(const uint32_t *words,char out[9]) {
    unsigned size=words[3];
    if (!size || size>8) return false;
    for (unsigned i=0;i<8;++i) {
        unsigned byte=(words[1+i/4]>>(8*(i%4)))&255u;
        if (i>=size) { if (byte) return false;continue; }
        if (!((byte>='a' && byte<='z') || (byte>='A' && byte<='Z') ||
              (byte>='0' && byte<='9') || byte==':' || byte=='_')) return false;
        out[i]=(char)byte;
    }
    out[size]=0;return true;
}
static bool aliases_command(const ctr_memory *memory,uint32_t output,uint32_t base) {
    uintptr_t target=0;
    for (size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];
        if (output>=map->base && (uint64_t)output<(uint64_t)map->base+map->size) {
            target=(uintptr_t)map->storage+(output-map->base);break;
        }
    }
    uint64_t end=(uint64_t)base+CTR_IPC_WORDS*4;
    for (size_t i=0;i<memory->count;++i) {
        const ctr_mapping *map=&memory->maps[i];
        uint64_t lo=base>map->base?base:map->base;
        uint64_t hi=end<(uint64_t)map->base+map->size?end:(uint64_t)map->base+map->size;
        if (lo<hi) {
            uintptr_t first=(uintptr_t)map->storage+(uintptr_t)(lo-map->base);
            if (target>=first && target-first<(uintptr_t)(hi-lo)) return true;
        }
    }
    return false;
}
void ctr_ipc_connect(ctr_guest *g) {
    char port[13]={0};uint32_t at=g->cpu.r[1];bool terminated=false;
    for (unsigned i=0;i<12;++i) {
        if ((uint64_t)at+i>UINT32_MAX) { memory_ok(g,CTR_MEM_OVERFLOW,at);return; }
        uint32_t byte;
        if (!memory_ok(g,ctr_memory_read(&g->memory,at+i,1,CTR_MEM_READ,&byte),at+i)) return;
        port[i]=(char)byte;
        if (!byte) { terminated=true;break; }
    }
    if (!terminated || strcmp(port,"srv:")) { unsupported(g,0x2d);return; }
    uint32_t handle=ctr_guest_session_open(g,CTR_SERVICE_SRV);
    g->cpu.r[0]=handle?0:NO_MEMORY;g->cpu.r[1]=handle;
}
void ctr_ipc_request(ctr_guest *g) {
    ++g->ipc_requests;g->ipc_header=0;g->ipc_result=0;g->ipc_service[0]=0;g->ipc_target[0]=0;
    ctr_guest_session *session=ctr_guest_session_for(g,g->cpu.r[0]);
    if (!session) { ++g->ipc_failures;g->ipc_result=INVALID_HANDLE;g->cpu.r[0]=INVALID_HANDLE;return; }
    static const char *const services[]={"srv:","cfg:u","hid:USER","gsp::Gpu","fs:USER","fs:FILE","APT:U"};
    strcpy(g->ipc_service,services[session->service]);
    uint64_t address=(uint64_t)g->cpu.tls_address+CTR_IPC_OFFSET;
    if (address>UINT32_MAX) { memory_ok(g,CTR_MEM_OVERFLOW,g->cpu.tls_address);return; }
    uint32_t base=(uint32_t)address,words[CTR_IPC_WORDS]={0};
    if (base&3u) { memory_ok(g,CTR_MEM_ALIGNMENT,base);return; }
    if (!memory_ok(g,ctr_memory_check_range(&g->memory,base,CTR_IPC_WORDS*4,
                   CTR_MEM_READ|CTR_MEM_WRITE),base)) return;
    if (!memory_ok(g,ctr_memory_read(&g->memory,base,4,CTR_MEM_READ,&words[0]),base)) return;
    g->ipc_header=words[0];
    unsigned count=1+((words[0]>>6)&63u)+(words[0]&63u);
    if ((words[0]&0xf000u) || count>CTR_IPC_WORDS) { unsupported(g,words[0]);return; }
    for (unsigned i=1;i<count;++i)
        if (!memory_ok(g,ctr_memory_read(&g->memory,base+i*4,4,CTR_MEM_READ,&words[i]),base+i*4)) return;
    uint32_t out[9]={0};
    if (session->service==CTR_SERVICE_SRV) {
        switch (words[0]) {
            case 0x00010002: /* RegisterClient with current-process-ID descriptor. */
                if (words[1]!=0x20) { unsupported(g,words[0]);return; }
                session->registered=true;out[0]=0x00010040;
                reply(g,base,out,2);return;
            case 0x00050100: { /* GetServiceHandle(name[8], length, flags). */
                char service[9]={0};
                if (!session->registered || words[4]>1 || !name(words,service)) { unsupported(g,words[0]);return; }
                strcpy(g->ipc_target,service);
                if (strcmp(service,"cfg:u") && strcmp(service,"hid:USER") && strcmp(service,"gsp::Gpu") && strcmp(service,"fs:USER") && (strcmp(service,"APT:U") || !g->apt.available)) {
                    out[0]=0x00050040;out[1]=SERVICE_NOT_FOUND;
                    ++g->ipc_failures;g->ipc_result=SERVICE_NOT_FOUND;
                    reply(g,base,out,2);return;
                }
                uint32_t handle=ctr_guest_session_open(g,!strcmp(service,"cfg:u")?CTR_SERVICE_CFG:!strcmp(service,"hid:USER")?CTR_SERVICE_HID:!strcmp(service,"fs:USER")?CTR_SERVICE_FS:!strcmp(service,"APT:U")?CTR_SERVICE_APT:CTR_SERVICE_GSP);
                if (!handle) {
                    out[0]=0x00050040;out[1]=NO_MEMORY;
                    ++g->ipc_failures;g->ipc_result=NO_MEMORY;
                    reply(g,base,out,2);return;
                }
                out[0]=0x00050042;out[2]=0x10;out[3]=handle;
                reply(g,base,out,4);return;
            }
            default: unsupported(g,words[0]);return;
        }
    }
    if(session->service==CTR_SERVICE_FS || session->service==CTR_SERVICE_FILE || session->service==CTR_SERVICE_APT) {
        unsigned count;
        bool ok=session->service==CTR_SERVICE_APT?ctr_apt_request(g,words,base,out,&count):ctr_fs_request(g,(unsigned)(session-g->sessions),words,base,out,&count);
        if(!ok) {
            if(g->state==CTR_GUEST_FAULTED)++g->ipc_failures;
            else unsupported(g,words[0]);
            return;
        }
        if(out[1]) { ++g->ipc_failures;g->ipc_result=out[1]; }
        reply(g,base,out,count);return;
    }
    if (session->service==CTR_SERVICE_HID) {
        if (words[0]!=0x000a0000) { unsupported(g,words[0]);return; }
        if (!ctr_hid_handles(g,out+3)) {
            out[0]=0x000a0040;out[1]=NO_MEMORY;++g->ipc_failures;g->ipc_result=NO_MEMORY;
            reply(g,base,out,2);return;
        }
        out[0]=0x000a0047;out[2]=5u<<26; /* Six copied handles. */
        reply(g,base,out,9);return;
    }
    if (session->service==CTR_SERVICE_GSP) {
        unsigned count;
        if(!ctr_gsp_request(g,(unsigned)(session-g->sessions),words,out,&count)) {
            if(g->state==CTR_GUEST_FAULTED)++g->ipc_failures;
            else unsupported(g,words[0]);
            return;
        }
        if(out[1]) { ++g->ipc_failures;g->ipc_result=out[1]; }
        reply(g,base,out,count);return;
    }
    switch (words[0]) {
        case 0x00020000: out[0]=0x00020080;out[2]=g->configuration.region;break;
        case 0x00050000: out[0]=0x00050080;out[2]=g->configuration.model;break;
        case 0x00060000: out[0]=0x00060080;out[2]=g->configuration.model==CTR_GUEST_MODEL_2DS?0:1;break;
        case 0x00010082: { /* GetConfigInfoBlk2: one-byte language block only. */
            if (words[1]!=1 || words[2]!=0x000a0002 || (words[3]!=0x1c && words[3]!=0x1e)) {
                unsupported(g,words[0]);return;
            }
            if (words[4]>=base && (uint64_t)words[4]<address+CTR_IPC_WORDS*4) {
                unsupported(g,words[0]);return;
            }
            unsigned rights=CTR_MEM_WRITE|(words[3]==0x1e?CTR_MEM_READ:0);
            if (!memory_ok(g,ctr_memory_check_range(&g->memory,words[4],1,rights),words[4])) return;
            if (aliases_command(&g->memory,words[4],base)) { unsupported(g,words[0]);return; }
            out[0]=0x00010042;out[2]=words[3];out[3]=words[4];
            if (!memory_ok(g,ctr_memory_write(&g->memory,words[4],1,g->configuration.language),words[4])) return;
            reply(g,base,out,4);return;
        }
        default: unsupported(g,words[0]);return;
    }
    reply(g,base,out,3);
}
