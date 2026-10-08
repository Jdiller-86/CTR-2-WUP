#include "ctr_service_fixture.h"
#include <string.h>

bool ctr_service_fixture_open(ctr_guest *g) {
    return ctr_guest_open(g,&ctr_service_image,ctr_service_fixture_blocks,ctr_service_fixture_block_count);
}
int ctr_service_selftest(void) {
    ctr_guest a,b;int result=0;
    if (!ctr_service_fixture_open(&a)) { ctr_guest_close(&a);return 1; }
    if (!ctr_service_fixture_open(&b)) { ctr_guest_close(&a);ctr_guest_close(&b);return 2; }
    if (ctr_guest_run(&a,1000,0,false)!=CTR_GUEST_EXITED ||
        ctr_guest_run(&b,1000,0,true)!=CTR_GUEST_EXITED) { result=3;goto done; }
    if (a.svc_count!=9 || a.ipc_requests!=5 || a.ipc_failures || b.cpu.fallback_instructions) { result=4;goto done; }
    for (unsigned offset=0x40;offset<=0x80;offset+=4) {
        uint32_t value,expected=offset==0x40?a.tls_address:offset==0x64?a.configuration.region:
                               offset==0x70?a.configuration.model:0;
        if (ctr_memory_read(&a.memory,0x08000000u+offset,4,CTR_MEM_READ,&value)!=CTR_MEM_OK ||
            (offset==0x58?!value:value!=expected)) { result=5;goto done; }
    }
    uint32_t language;
    if (ctr_memory_read(&a.memory,0x08000020,1,CTR_MEM_READ,&language)!=CTR_MEM_OK ||
        language!=a.configuration.language) { result=6;goto done; }
    if (a.cpu.instructions!=b.cpu.instructions || a.cpu.cpsr!=b.cpu.cpsr ||
        memcmp(a.cpu.r,b.cpu.r,sizeof(a.cpu.r)) || a.ipc_requests!=b.ipc_requests ||
        a.svc_count!=b.svc_count || b.ipc_failures) { result=7;goto done; }
    for (unsigned i=0;i<5;++i) if (a.storage_size[i]!=b.storage_size[i] ||
        (a.storage_size[i] && memcmp(a.storage[i],b.storage[i],a.storage_size[i]))) { result=8;goto done; }
done:
    ctr_guest_close(&a);ctr_guest_close(&b);return result;
}
