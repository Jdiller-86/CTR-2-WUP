#include "ctr_dsp_fixture.h"
#include <stdlib.h>
bool ctr_dsp_fixture_render(bool translated,int16_t *pcm,size_t samples,
                            ctr_dsp_statistics *statistics,uint64_t *instructions) {
    if(!pcm || samples<CTR_DSP_FIXTURE_BLOCKS*CTR_DSP_FRAME_SAMPLES*2 || !statistics || !instructions)return false;
    ctr_guest *g=calloc(1,sizeof(*g));if(!g)return false;
    bool ok=ctr_guest_open(g,&ctr_dsp_image,ctr_dsp_fixture_blocks,ctr_dsp_fixture_block_count);
    uint64_t now=0;unsigned blocks=0;
    for(unsigned turns=0;ok && turns<1000;++turns) {
        ctr_guest_state state=ctr_guest_run(g,1000000,now,translated);
        if(state==CTR_GUEST_FAULTED) { ok=false;break; }
        while(blocks<CTR_DSP_FIXTURE_BLOCKS && ctr_dsp_read(g,pcm+blocks*CTR_DSP_FRAME_SAMPLES*2,CTR_DSP_FRAME_SAMPLES*2))++blocks;
        if(state==CTR_GUEST_EXITED)break;
        /* One original frame every 5ms host scheduling quantum. The DSP keeps
         * its own exact 160-sample deadline and does not execute early. */
        now+=5000000;
    }
    bool recorded=ctr_dsp_stats(g,statistics);
    ok=ok && g->state==CTR_GUEST_EXITED && blocks==CTR_DSP_FIXTURE_BLOCKS && !g->ipc_failures && recorded;
    *instructions=g->cpu.instructions;ctr_guest_close(g);free(g);return ok;
}
