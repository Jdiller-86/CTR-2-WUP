#include "guest_slice.h"
#include <stddef.h>
ctr_slice_report ctr_guest_slice(const ctr_slice_policy *p,ctr_slice_clock clock,ctr_slice_execute execute,void *context) {
    ctr_slice_report r={0,0,0,CTR_SLICE_ARGUMENT};
    if(!p || !clock || !execute || !p->quantum || !p->max_calls || p->max_calls>10000u ||
       !p->max_instructions || !p->max_us)return r;
    uint64_t start=clock(context),previous=start;r.reason=CTR_SLICE_CALLS;
    for(unsigned call=0;call<p->max_calls;++call) {
        uint64_t now=clock(context);
        if(now<previous) { r.reason=CTR_SLICE_CLOCK;return r; }
        r.elapsed_us=now-start;previous=now;
        if(r.elapsed_us>=p->max_us) { r.reason=CTR_SLICE_TIME;return r; }
        uint64_t remaining=p->max_instructions-r.instructions;
        if(!remaining) { r.reason=CTR_SLICE_INSTRUCTIONS;return r; }
        unsigned budget=remaining<p->quantum?(unsigned)remaining:p->quantum;
        ctr_slice_step step=execute(context,budget);++r.calls;
        now=clock(context);
        if(now<previous) { r.reason=CTR_SLICE_CLOCK;return r; }
        r.elapsed_us=now-start;previous=now;
        if(step.instructions>budget || (unsigned)step.state>CTR_SLICE_STOP) { r.reason=CTR_SLICE_CONTRACT;return r; }
        r.instructions+=step.instructions;
        if(step.frame) { r.reason=CTR_SLICE_FRAME;return r; }
        if(step.state!=CTR_SLICE_RUN) {
            r.reason=step.state==CTR_SLICE_WAIT?CTR_SLICE_WAITING:
                     step.state==CTR_SLICE_YIELD?CTR_SLICE_YIELDED:CTR_SLICE_STOPPED;
            return r;
        }
        if(!step.instructions) { r.reason=CTR_SLICE_NO_PROGRESS;return r; }
        if(r.elapsed_us>=p->max_us) { r.reason=CTR_SLICE_TIME;return r; }
    }
    if(r.instructions==p->max_instructions)r.reason=CTR_SLICE_INSTRUCTIONS;
    return r;
}
const char *ctr_slice_reason_name(ctr_slice_reason reason) {
    static const char *const names[]={"instructions","soft-time","calls","new-frame","waiting",
        "yielded","stopped","no-progress","clock-backwards","callback-contract","argument"};
    return (unsigned)reason<sizeof(names)/sizeof(*names)?names[reason]:"unknown";
}
bool ctr_draw_observation_ready(bool decoded,uint64_t serial,uint64_t draws,uint64_t triangles,
                                uint64_t fragments,uint64_t shader_steps) {
    return decoded && serial>=2 && draws==4 && triangles==4 && fragments==97200 && shader_steps==36;
}
bool ctr_textured_draw_observation_ready(bool decoded,uint64_t serial,uint64_t draws,uint64_t triangles,
                                         uint64_t fragments,uint64_t shader_steps) {
    return decoded && serial>=2 && draws==10 && triangles==10 && fragments==237540 && shader_steps==120;
}
