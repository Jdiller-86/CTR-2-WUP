#ifndef CTR_GUEST_SLICE_H
#define CTR_GUEST_SLICE_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { CTR_SLICE_RUN, CTR_SLICE_WAIT, CTR_SLICE_YIELD, CTR_SLICE_STOP } ctr_slice_state;
typedef enum { CTR_SLICE_INSTRUCTIONS, CTR_SLICE_TIME, CTR_SLICE_CALLS, CTR_SLICE_FRAME,
    CTR_SLICE_WAITING, CTR_SLICE_YIELDED, CTR_SLICE_STOPPED, CTR_SLICE_NO_PROGRESS,
    CTR_SLICE_CLOCK, CTR_SLICE_CONTRACT, CTR_SLICE_ARGUMENT } ctr_slice_reason;
typedef struct { unsigned quantum,max_calls;uint64_t max_instructions,max_us; } ctr_slice_policy;
typedef struct { uint64_t instructions;ctr_slice_state state;bool frame; } ctr_slice_step;
typedef struct { unsigned calls;uint64_t instructions,elapsed_us;ctr_slice_reason reason; } ctr_slice_report;
typedef uint64_t (*ctr_slice_clock)(void *);
typedef ctr_slice_step (*ctr_slice_execute)(void *,unsigned);
/* Cooperative soft wall-time bound, checked between bounded instruction calls.
 * One generated block/SVC may overrun wall time. No clock is synthesized and no
 * waiting/yielding/no-progress callback is retried within this refresh. */
ctr_slice_report ctr_guest_slice(const ctr_slice_policy *,ctr_slice_clock,ctr_slice_execute,void *);
const char *ctr_slice_reason_name(ctr_slice_reason);
bool ctr_draw_observation_ready(bool decoded,uint64_t serial,uint64_t draws,uint64_t triangles,
                                uint64_t fragments,uint64_t shader_steps);
#endif
