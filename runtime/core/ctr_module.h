#ifndef CTR_MODULE_H
#define CTR_MODULE_H
#include "ctr_guest.h"

#define CTR_MODULE_MAX_IMAGES 8u
#define CTR_MODULE_MAX_FILE (4u*1024u*1024u)
/* Package-owned immutable CRO0 bytes and code translated after relocation at
 * these fixed guest addresses. A CRS describes the main executable's exports.
 * Descriptors, bytes, code witnesses and block tables must outlive the guest. */
typedef struct {
    const uint8_t *bytes;
    uint32_t size,address,data_address,bss_address;
    bool is_static;
    const uint8_t *code;
    uint32_t code_size;
    const ctr_block *blocks;
    size_t block_count;
    const char *identity;
} ctr_module_image;
typedef enum {
    CTR_MODULE_OK=0, CTR_MODULE_FORMAT, CTR_MODULE_UNSUPPORTED,
    CTR_MODULE_IDENTITY, CTR_MODULE_MEMORY, CTR_MODULE_CAPACITY,
    CTR_MODULE_STATE, CTR_MODULE_SYMBOL, CTR_MODULE_BUSY, CTR_MODULE_INVALIDATED
} ctr_module_result;

bool ctr_modules_configure(ctr_guest *guest,const ctr_module_image *images,size_t count);
void ctr_modules_close(ctr_guest *guest);
size_t ctr_modules_bytes(const ctr_guest *guest);
ctr_module_result ctr_modules_load(ctr_guest *guest,uint32_t source,uint32_t address,uint32_t size,
    uint32_t data,uint32_t data_size,uint32_t bss,uint32_t bss_size,bool link,bool is_static);
ctr_module_result ctr_modules_link(ctr_guest *guest,uint32_t address,bool link);
ctr_module_result ctr_modules_unload(ctr_guest *guest,uint32_t address,uint32_t source,bool is_static);
ctr_module_result ctr_modules_lookup(ctr_guest *guest,uint32_t address,const char *name,uint32_t *value);
/* Shared original-AOT dispatch; the ordinary path is retained without modules. */
ctr_cpu_result ctr_modules_dispatch(ctr_guest *guest,uint64_t budget);
ctr_cpu_result ctr_modules_interpret(ctr_guest *guest,uint64_t budget);
bool ctr_modules_has_code(const ctr_guest *guest,uint32_t address);
bool ctr_modules_generation_current(const ctr_guest *guest);
bool ctr_modules_mapping(const ctr_guest *guest,const ctr_mapping *mapping);
bool ctr_modules_request(ctr_guest *guest,unsigned session,const uint32_t *words,
                         uint32_t command_base,uint32_t *out,unsigned *count);
const char *ctr_module_result_name(ctr_module_result result);
#endif
