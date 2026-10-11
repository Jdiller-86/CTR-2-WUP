#ifndef CTR_TEST_LIFECYCLE_H
#define CTR_TEST_LIFECYCLE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { CTR_EXIT_NONE, CTR_EXIT_HBL, CTR_EXIT_MENU } ctr_exit_action;
typedef struct { uint64_t title_id; bool requested; } ctr_test_lifecycle;
/* WUT's libwhb recognizes HBL and the three regional Mii Maker launchers.
 * Menu-launched titles must request system exit, then continue ProcUI polling. */
bool ctr_test_from_hbl(uint64_t title_id);
ctr_exit_action ctr_test_request_exit(ctr_test_lifecycle *lifecycle);
#endif
