#ifndef CTR_TEST_RESULT_H
#define CTR_TEST_RESULT_H
#include <stdbool.h>
#include <stdint.h>

#define CTR_TEST_RESULT_BYTES 4096u
typedef struct {
    bool ready,failed;
    int error_code;
    const char *error_stage;
    uint64_t verified_writes;
    char filename[128],path[512],identity[384];
} ctr_test_result;

/* A separate, fixed-size result file, independent of WHB's trace handlers.
 * Uses the closed binary write/read pattern of the persisted save snapshot.
 * Exclusive per launch; updates only the reserved file after checking identity.
 * VERIFIED means byte-for-byte read-back, not survival of an SD power loss. */
bool ctr_test_result_open(ctr_test_result *result,const char *directory,
                         const char *application,const char *version,uint64_t id);
bool ctr_test_result_update(ctr_test_result *result,const char *body);
#endif
