#include "test_lifecycle.h"

bool ctr_test_from_hbl(uint64_t id) {
    return id==UINT64_C(0x0005000013374842) || id==UINT64_C(0x000500101004a000) ||
        id==UINT64_C(0x000500101004a100) || id==UINT64_C(0x000500101004a200);
}
ctr_exit_action ctr_test_request_exit(ctr_test_lifecycle *lifecycle) {
    if(!lifecycle || lifecycle->requested)return CTR_EXIT_NONE;
    lifecycle->requested=true;
    return ctr_test_from_hbl(lifecycle->title_id)?CTR_EXIT_HBL:CTR_EXIT_MENU;
}
