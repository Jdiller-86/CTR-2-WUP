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

bool ctr_test_complete_exit(ctr_test_lifecycle *state,bool exit_observed,
                            const ctr_exit_ops *ops,void *context) {
    if(!state || !ops || !ops->cleanup_resources || !ops->clear_callbacks ||
        !ops->request_menu || !ops->process_messages || !ops->release_foreground ||
        !ops->wait || !ops->shutdown_procui || !ops->shutdown_whb || !ops->checkpoint)
        return false;
    if(state->finished)return true;
    ops->cleanup_resources(context);
    ops->checkpoint(context,"EXIT stage=resources_closed_before_procui_shutdown");
    ops->clear_callbacks(context);
    ops->checkpoint(context,"EXIT stage=callbacks_cleared");
    if(ops->finalize_io) {
        ops->checkpoint(context,"EXIT stage=runtime_io_finalize_begin; final SD checkpoint; later stages use OSReport");
        ops->finalize_io(context);
    }
    ctr_exit_action route=ctr_test_request_exit(state);
    if(!exit_observed && route==CTR_EXIT_MENU) {
        ops->checkpoint(context,"EXIT stage=request_menu_begin");
        ops->request_menu(context);
        ops->checkpoint(context,"EXIT stage=request_menu_returned");
        ctr_proc_status previous=(ctr_proc_status)-1;
        for(;;) {
            if(previous==(ctr_proc_status)-1)ops->checkpoint(context,"EXIT stage=procui_poll_begin");
            ctr_proc_status status=ops->process_messages(context);
            if(status!=previous) {
                const char *message=status==CTR_PROC_FOREGROUND?"EXIT status=FOREGROUND":
                    status==CTR_PROC_BACKGROUND?"EXIT status=BACKGROUND":
                    status==CTR_PROC_RELEASE?"EXIT status=RELEASE_FOREGROUND":
                    status==CTR_PROC_EXITING?"EXIT status=EXITING":"EXIT status=UNRECOGNIZED; still waiting";
                ops->checkpoint(context,message);previous=status;
            }
            if(status==CTR_PROC_EXITING)break;
            if(status==CTR_PROC_RELEASE) {
                ops->checkpoint(context,"EXIT stage=draw_done_release_begin");
                ops->release_foreground(context);
                ops->checkpoint(context,"EXIT stage=draw_done_release_returned");
            }
            ops->wait(context);
        }
    }
    ops->checkpoint(context,"EXIT stage=procui_shutdown_begin");
    ops->shutdown_procui(context);
    ops->checkpoint(context,"EXIT stage=procui_shutdown_done");
    ops->checkpoint(context,"EXIT stage=whb_proc_shutdown_begin");
    ops->shutdown_whb(context);
    ops->checkpoint(context,"EXIT stage=whb_proc_shutdown_done");
    ops->checkpoint(context,"EXIT stage=main_return; runtime finalizers and launcher return still require observation");
    state->finished=true;
    return true;
}
