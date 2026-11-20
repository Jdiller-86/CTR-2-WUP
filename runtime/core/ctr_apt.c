/* Original standalone APT state machine; wire facts from pinned libctru. */
#include "ctr_apt.h"
#include "ctr_fs.h"
#include "ctr_kernel.h"
#include <string.h>
#define STATE UINT32_C(0xc8a06c00)
#define EMPTY UINT32_C(0xc8806c02)
#define FULL UINT32_C(0xd86007f3)
#define APP_MII_SELECTOR 0x402u
#define MII_CONFIG_BYTES 0x104u
#define MII_RESULT_BYTES 0x84u
#define CAPTURE_INFO_BYTES 0x20u
enum { MII_IDLE, MII_PRELOADED, MII_PREPARED, MII_CAPTURE_REPLY, MII_CAPTURED,
       MII_RESULT, MII_RESUMED };

bool ctr_apt_mii_capture_ready(const ctr_guest *g) {
    return g->apt.mii_phase==MII_PRELOADED || g->apt.mii_phase==MII_PREPARED;
}

static void mii_reset(ctr_guest *g) {
    if(g->apt.mii_capture)ctr_guest_handle_close(g,g->apt.mii_capture);
    g->apt.mii_capture=0;
    g->apt.mii_phase=MII_IDLE;
    g->apt.mii_vram_saved=false;
}

bool ctr_apt_configure(ctr_guest *g) {
    if(!g || g->state!=CTR_GUEST_READY || g->cpu.instructions || g->svc_count || g->apt.available)return false;
    g->apt.available=true;return true;
}
void ctr_apt_close(ctr_guest *g) {
    mii_reset(g);
    if(g->apt.initialized)for(unsigned i=0;i<2;++i)ctr_guest_object_release(g,CTR_HANDLE_EVENT,g->apt.events[i]);
    if(g->apt.lock)ctr_guest_object_release(g,CTR_HANDLE_MUTEX,g->apt.mutex);
    memset(&g->apt,0,sizeof(g->apt));
}
static bool memory(ctr_guest *g,ctr_mem_result result,uint32_t at) {
    if(result==CTR_MEM_OK)return true;
    g->state=CTR_GUEST_FAULTED;g->fault=CTR_CPU_MEMORY_FAULT;g->fault_detail=at;g->cpu.memory_fault=result;
    ctr_cpu_stop(&g->cpu,CTR_CPU_MEMORY_FAULT,at);return false;
}
static bool static_output(ctr_guest *g,uint32_t size,uint32_t base,uint32_t *address) {
    uint32_t descriptor,at=g->cpu.tls_address+0x180;
    if(size>4096 || !memory(g,ctr_memory_read(&g->memory,at,4,CTR_MEM_READ,&descriptor),at) ||
       !memory(g,ctr_memory_read(&g->memory,at+4,4,CTR_MEM_READ,address),at+4))return false;
    if(descriptor!=((size<<14)|2u))return false;
    return ctr_fs_buffer(g,*address,size,CTR_MEM_WRITE,base);
}

static bool static_input(ctr_guest *g,uint32_t size,uint32_t descriptor,uint32_t address,
                         uint32_t base,uint8_t *data) {
    return descriptor==((size<<14)|2u) && ctr_fs_buffer(g,address,size,CTR_MEM_READ,base) &&
           memory(g,ctr_memory_read_bytes(&g->memory,address,data,size),address);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static uint32_t capture_size(const uint8_t info[CAPTURE_INFO_BYTES]) {
    const unsigned pixel_bytes[]={4,3,2,2,2};
    uint32_t size=le32(info),stereo=le32(info+4),top_format=le32(info+16),bottom_format=le32(info+28);
    if(!size || size>2u*1024u*1024u || stereo>1 || top_format>4 || bottom_format>4)return 0;
    uint32_t top=256u*400u*pixel_bytes[top_format],bottom=256u*320u*pixel_bytes[bottom_format];
    if(top>size || bottom>size || le32(info+8)>size-top || le32(info+12)>size-top ||
       le32(info+20)>size-bottom || le32(info+24)>size-bottom)return 0;
    return (size+CTR_GUEST_PAGE-1u)&~(CTR_GUEST_PAGE-1u);
}

/* The standalone app has no system Mii database. When a guest is allowed,
 * return an original local preset in the selector's documented wire format. */
static void mii_result(uint8_t selected,uint8_t data[MII_RESULT_BYTES]) {
    static const char name[]="Guest";
    uint8_t *mii=data+0x0c;
    data[7]=1;
    data[11]=selected;
    mii[0]=3;
    mii[3]=0x30;
    memcpy(mii+4,"CTR2WUP",7);
    mii[0x0c]=selected+1u;
    mii[0x0f]=0x80;
    mii[0x10]=2; /* Locally administered creator address. */
    mii[0x18]=0x22; /* January 1. */
    mii[0x19]=0x0c; /* Green shirt. */
    mii[0x2e]=64;
    mii[0x2f]=64;
    mii[0x30]=1; /* Sharing disabled. */
    mii[0x32]=24;
    mii[0x33]=2;
    /* Eyes, eyebrows, nose and mouth. Multibyte appearance fields are LE. */
    mii[0x34]=0x48; mii[0x35]=0x48; mii[0x36]=0x85; mii[0x37]=3;
    mii[0x38]=0x46; mii[0x39]=0x44; mii[0x3a]=0x6a; mii[0x3b]=4;
    mii[0x3c]=0x82; mii[0x3d]=0x1c;
    mii[0x3e]=0x45; mii[0x3f]=0x68;
    mii[0x40]=18;
    for(unsigned i=0;i<sizeof(name)-1u;++i) {
        mii[0x1a+i*2u]=(uint8_t)name[i];
        data[0x6c+i*2u]=(uint8_t)name[i];
    }
    uint16_t crc=0;
    for(unsigned i=0;i<0x5eu;++i) {
        crc^=(uint16_t)mii[i]<<8;
        for(unsigned bit=0;bit<8;++bit)crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0u));
    }
    data[0x6a]=(uint8_t)(crc>>8);
    data[0x6b]=(uint8_t)crc;
}

static void wakeup(ctr_guest *g,bool notified) {
    g->apt.pending=true;
    g->apt.notified=notified;
    g->events[g->apt.events[1]].signaled=true;
    ctr_threads_poll(g);
}

bool ctr_apt_request(ctr_guest *g,const uint32_t *w,uint32_t base,uint32_t *o,unsigned *count) {
    o[0]=(w[0]&0xffff0000u)|0x40u;*count=2;
    if(!g->apt.available) { o[1]=STATE;return true; }
    if(w[0]==0x00010040) {
        if(w[1])return false;
        bool created=false;
        if(!g->apt.lock) {
            unsigned i;for(i=0;i<16 && g->mutexes[i].active;++i){}
            if(i==16) { o[1]=FULL;return true; }
            g->mutexes[i]=(ctr_guest_mutex){.active=true,.references=1};g->apt.mutex=i;g->apt.lock=true;created=true;
        }
        uint32_t h=ctr_guest_handle_new(g,CTR_HANDLE_MUTEX,g->apt.mutex);
        if(!h) { if(created) { ctr_guest_object_release(g,CTR_HANDLE_MUTEX,g->apt.mutex);g->apt.lock=false; }o[1]=FULL;return true; }
        o[0]=0x000100c2;o[4]=0;o[5]=h;*count=6;return true;
    }
    if(w[0]==0x00020080) {
        if(w[1]!=0x300 || w[2])return false;
        if(!g->apt.lock || g->apt.initialized) { o[1]=STATE;return true; }
        uint32_t h[2]={ctr_guest_event_create(g,0),0};if(h[0])h[1]=ctr_guest_event_create(g,0);
        if(!h[0] || !h[1]) { if(h[0])ctr_guest_handle_close(g,h[0]);o[1]=FULL;return true; }
        for(unsigned i=0;i<2;++i) { g->apt.events[i]=ctr_guest_handle_for(g,h[i])->object;ctr_guest_object_retain(g,CTR_HANDLE_EVENT,g->apt.events[i]); }
        g->apt.initialized=true;o[0]=0x00020043;o[2]=1u<<26;o[3]=h[0];o[4]=h[1];*count=5;return true;
    }
    if(!g->apt.initialized) { o[1]=STATE;return true; }
    switch(w[0]) {
        case 0x00030040:
            if(w[1])return false;
            if(g->apt.enabled) { o[1]=STATE;return true; }
            g->apt.enabled=true;g->apt.transitions=0x10;wakeup(g,false);return true;
        case 0x00430040:
            if(w[1]!=0x300)return false;
            if(!g->apt.enabled || !g->apt.pending) { o[1]=STATE;return true; }
            g->apt.notified=true;return true;
        case 0x000e0080: case 0x000d0080: {
            uint32_t at;if(w[1]!=0x300 || !static_output(g,w[2],base,&at))return false;
            if(!g->apt.pending || !g->apt.notified) { o[1]=EMPTY;return true; }
            bool consume=w[0]==0x000d0080;
            o[0]=(w[0]&0xffff0000u)|0x102u;o[3]=1;*count=7;
            if(g->apt.mii_phase==MII_CAPTURE_REPLY) {
                uint32_t handle=g->apt.mii_capture;
                if(!consume) {
                    ctr_guest_handle *saved=ctr_guest_handle_for(g,handle);
                    if(!saved)return false;
                    handle=ctr_guest_handle_new(g,CTR_HANDLE_SHARED,saved->object);
                    if(!handle) { o[0]=(w[0]&0xffff0000u)|0x40u;o[1]=FULL;*count=2;return true; }
                } else {
                    g->apt.mii_capture=0;
                    g->apt.mii_phase=MII_CAPTURED;
                }
                o[2]=APP_MII_SELECTOR;o[3]=3;o[5]=0x10;o[6]=handle;
            } else if(g->apt.mii_phase==MII_RESULT) {
                uint8_t data[MII_RESULT_BYTES]={0};
                uint32_t size=w[2]<sizeof(data)?w[2]:sizeof(data);
                mii_result(g->apt.mii_guest,data);
                if(size && !memory(g,ctr_memory_write_bytes(&g->memory,at,data,size),at))return false;
                o[2]=APP_MII_SELECTOR;o[3]=10;o[4]=size;
                if(consume)g->apt.mii_phase=MII_RESUMED;
            }
            if(consume) { g->apt.pending=false;g->events[g->apt.events[1]].signaled=false; }
            return true;
        }
        case 0x00160040: /* PreloadLibraryApplet. */
            if(w[1]!=APP_MII_SELECTOR)return false;
            if(!g->fs_mii_romfs || !g->apt.enabled || g->apt.pending || g->apt.mii_vram_saved ||
               (g->apt.mii_phase!=MII_IDLE && g->apt.mii_phase!=MII_RESUMED)) { o[1]=STATE;return true; }
            g->apt.mii_phase=MII_PRELOADED;return true;
        case 0x00170040: /* FinishPreloadingLibraryApplet. */
        case 0x00180040: /* PrepareToStartLibraryApplet. */
            if(w[1]!=APP_MII_SELECTOR)return false;
            if(g->apt.mii_phase!=MII_PRELOADED) { o[1]=STATE;return true; }
            if(w[0]==0x00180040)g->apt.mii_phase=MII_PREPARED;
            return true;
        case 0x00060040: /* GetAppletInfo. */
            if(w[1]!=APP_MII_SELECTOR)return false;
            if(g->apt.mii_phase<MII_PRELOADED || g->apt.mii_phase>=MII_RESULT) { o[1]=STATE;return true; }
            o[0]=0x000601c0;
            /* The preloaded local selector has library position, with no separate process. */
            o[5]=1;o[6]=1;o[7]=1;*count=8;return true;
        case 0x00090040: /* IsRegistered. */
            if(w[1]!=APP_MII_SELECTOR)return false;
            o[0]=0x00090080;
            o[2]=g->apt.mii_phase>=MII_PRELOADED && g->apt.mii_phase<MII_RESULT;
            *count=3;return true;
        case 0x000c0104: { /* SendParameter, display capture allocation request. */
            uint8_t info[CAPTURE_INFO_BYTES];
            if(w[1]!=0x300 || w[2]!=APP_MII_SELECTOR || w[3]!=2 || w[4]!=sizeof(info) || w[5] || w[6] ||
               !static_input(g,sizeof(info),w[7],w[8],base,info))return false;
            if(!ctr_apt_mii_capture_ready(g) || g->apt.pending) { o[1]=STATE;return true; }
            uint32_t size=capture_size(info);
            if(!size)return false;
            uint32_t handle=ctr_shared_create_capture_buffer(g,size);
            if(!handle) { o[1]=FULL;return true; }
            g->apt.mii_capture=handle;g->apt.mii_phase=MII_CAPTURE_REPLY;wakeup(g,true);return true;
        }
        case 0x00400042: { /* SendCaptureBufferInfo. */
            uint8_t info[CAPTURE_INFO_BYTES];
            if(w[1]!=sizeof(info) || !static_input(g,sizeof(info),w[2],w[3],base,info) || !capture_size(info))return false;
            if(g->apt.mii_phase!=MII_CAPTURED)o[1]=STATE;
            return true;
        }
        case 0x001e0084: { /* StartLibraryApplet with the Mii Selector configuration. */
            uint8_t config[MII_CONFIG_BYTES];
            if(w[1]!=APP_MII_SELECTOR || w[2]!=sizeof(config) || w[3] || w[4] ||
               !static_input(g,sizeof(config),w[5],w[6],base,config) || le32(config+0x100)!=0x13de28cfu)return false;
            if(g->apt.mii_phase!=MII_CAPTURED || g->apt.pending || g->gsp.right) { o[1]=STATE;return true; }
            uint32_t selected=config[0x8c]?le32(config+0x90):0u;
            if(!config[1])return false;
            if(selected>=6 || !config[0x94+selected]) {
                for(selected=0;selected<6 && !config[0x94+selected];++selected){}
            }
            if(selected==6)return false;
            g->apt.mii_guest=(uint8_t)selected;
            g->apt.mii_phase=MII_RESULT;g->apt.transitions|=0x10;wakeup(g,true);return true;
        }
        case 0x004b00c2: {
            uint32_t at;if(!static_output(g,w[3],base,&at) || w[3]!=1 || w[4]!=((w[2]<<14)|0x402u))return false;
            uint32_t required=w[1]==4?1:w[1]==7?4:0;
            if(!required || w[2]!=required || !ctr_fs_buffer(g,w[5],required,CTR_MEM_READ,base))return false;
            uint32_t input;if(!memory(g,ctr_memory_read(&g->memory,w[5],required==1?1:4,CTR_MEM_READ,&input),w[5]))return false;
            if((w[1]==4 && input) || (w[1]==7 && input!=0x10 &&
                                     !(input==1 && g->apt.mii_phase==MII_RESUMED)))return false;
            /* The standalone runtime has no shell-closed condition. Unlocking
               the enable transition changes the explicit local state. */
            if(!memory(g,ctr_memory_write(&g->memory,at,1,0),at))return false;
            if(w[1]==7)g->apt.transitions&=~input;
            o[0]=0x004b0080;o[2]=0;*count=3;return true;
        }
        case 0x003e0080: /* ReplySleepQuery for the standalone application. */
            if(w[1]!=0x300 || w[2]>2u)return false;
            if(!g->apt.enabled) { o[1]=STATE;return true; }
            /* The host owns suspension. The guest's local reply cannot
             * initiate sleep or change the Wii U process lifecycle. */
            return true;
        case 0x00220040:
            if(w[1]>1)return false;
            if(!g->apt.enabled || g->apt.pending || g->apt.closing) { o[1]=STATE;return true; }
            g->apt.closing=true;return true;
        case 0x00270044:
            if(w[1] || w[2] || w[3] || w[4]!=2)return false;
            if(!g->apt.closing) { o[1]=STATE;return true; }
            ctr_threads_exit_process(g);return true;
        case 0x00040040:
            if(w[1]!=0x300)return false;
            mii_reset(g);
            /* Finalize unregisters this application; existing copied handles
               retain their objects independently of the manager. */
            for(unsigned i=0;i<2;++i)ctr_guest_object_release(g,CTR_HANDLE_EVENT,g->apt.events[i]);
            g->apt.initialized=false;g->apt.enabled=false;g->apt.pending=false;g->apt.notified=false;g->apt.closing=false;return true;
        case 0x01020000:o[0]=0x01020080;o[2]=0;*count=3;return true; /* Old-family policy. */
        default:return false;
    }
}
