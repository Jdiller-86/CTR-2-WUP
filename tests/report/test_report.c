#define _POSIX_C_SOURCE 200809L
#include "test_report.h"
#include "test_guide.h"
#include "test_lifecycle.h"
#include "image_scale.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %u line %d: %s\n", checks, __LINE__, #x); exit(1); } } while (0)

static char guide_log_text[32768];
static void capture(void *context,const char *message) {
    (void)context;
    size_t used=strlen(guide_log_text),n=strlen(message);
    CHECK(used+n+2<sizeof(guide_log_text));
    memcpy(guide_log_text+used,message,n);guide_log_text[used+n]='\n';guide_log_text[used+n+1]=0;
}
static void touches(ctr_test_guide *g,ctr_guide_input *in) {
    static const uint16_t points[5][2]={{0,0},{319,0},{0,239},{319,239},{160,120}};
    in->pressed=0;in->touch_inside=true;
    for(unsigned n=0;n<5;++n) { in->touch_x=points[n][0];in->touch_y=points[n][1];ctr_test_guide_update(g,in); }
    in->touch_inside=false;
}
static void guide_tests(void) {
    ctr_test_guide g;ctr_guide_input in={0};char view[7][72];
    ctr_test_guide_init(&g,CTR_GUIDE_NATIVE,capture,NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==1);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[4],"Need: A B X Y L R")!=NULL);
    in.pressed=CTR_GUIDE_A|CTR_GUIDE_B|CTR_GUIDE_X|CTR_GUIDE_Y|CTR_GUIDE_L|CTR_GUIDE_R|
        CTR_GUIDE_LEFT|CTR_GUIDE_RIGHT|CTR_GUIDE_UP|CTR_GUIDE_DOWN;
    ctr_test_guide_update(&g,&in);CHECK(g.step==2);
    in.pressed=0;in.left_x=-1;in.left_y=-1;ctr_test_guide_update(&g,&in);CHECK(g.step==2);
    in.left_x=1;in.left_y=1;ctr_test_guide_update(&g,&in);CHECK(g.step==3);
    in.right_x=-1;in.right_y=-1;ctr_test_guide_update(&g,&in);
    in.right_x=1;in.right_y=1;ctr_test_guide_update(&g,&in);CHECK(g.step==4);
    touches(&g,&in);CHECK(g.step==5);
    in.touch_border=true;ctr_test_guide_update(&g,&in);CHECK(g.step==6);in.touch_border=false;
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==7);
    ctr_test_guide_update(&g,&in);CHECK(g.step==7); /* Confirmation without requesting sound cannot pass. */
    in.pressed=CTR_GUIDE_A;ctr_test_guide_update(&g,&in);
    in.pressed=CTR_GUIDE_R;in.milliseconds=10;ctr_test_guide_update(&g,&in);CHECK(g.step==8);
    in.pressed=0;in.milliseconds=0;ctr_test_guide_update(&g,&in);CHECK(g.step==8); /* No backwards-time underflow. */
    in.milliseconds=600009;ctr_test_guide_update(&g,&in);CHECK(g.step==8);
    in.milliseconds=600010;ctr_test_guide_update(&g,&in);CHECK(g.step==9);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==10 && !g.failed && !g.skipped);
    ctr_test_guide_view(&g,in.milliseconds,view);CHECK(strstr(view[3],"SD:/CTR2WUP/*.TXT")!=NULL);
    CHECK(strstr(view[4],"Upload")!=NULL);
    CHECK(strstr(guide_log_text,"User confirmed")!=NULL && strstr(guide_log_text,"ten requested buttons")!=NULL);
    ctr_test_guide_summary(&g);CHECK(strstr(guide_log_text,"not_finished=0")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_NATIVE,capture,NULL);
    in.pressed=CTR_GUIDE_L;ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
    in.pressed=CTR_GUIDE_SKIP;ctr_test_guide_update(&g,&in);CHECK(g.skipped==1 && g.step==2);
    in.pressed=CTR_GUIDE_MINUS;ctr_test_guide_update(&g,&in);CHECK(g.cancelled && !g.done);
    ctr_test_guide_summary(&g);CHECK(strstr(guide_log_text,"NOT TESTED")!=NULL && strstr(guide_log_text,"not_finished=8")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_SAVE,capture,NULL);
    in.guest_sessions=1;in.guest_exited=true;in.guest_pass=true;
    ctr_test_guide_update(&g,&in);CHECK(g.step==1);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    CHECK(!ctr_test_guide_take_restart(&g));
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(!g.done); /* Old process PASS does not establish reload. */
    in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==2);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[6],"BOTH reports")!=NULL);
    ctr_test_guide_init(&g,CTR_GUIDE_SAVE,capture,NULL);in.guest_sessions=1;in.fresh_save_reload=true;
    ctr_test_guide_update(&g,&in);CHECK(g.fresh_reload_confirmed);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    in.pressed=0;in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.done);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[6],"testing complete")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_SAVE,capture,NULL);
    in.guest_exited=true;ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
    in.pressed=CTR_GUIDE_FAIL;ctr_test_guide_update(&g,&in);CHECK(g.done && g.failed==2 && !g.passed);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_INTERACTIVE,capture,NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==0); /* No rendered frame yet. */
    in.guest_frame_available=true;ctr_test_guide_update(&g,&in);CHECK(g.step==1);
    ctr_test_guide_update(&g,&in);CHECK(g.step==1); /* A must be exercised before confirmation. */
    in.held=CTR_GUIDE_A;in.input_frame_serial=1;in.guest_frame_serial=1;
    ctr_test_guide_update(&g,&in);CHECK(g.step==1); /* R cannot pass before a new green frame. */
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[4],"WAITING FOR GREEN")!=NULL);
    in.guest_green_frame=true;ctr_test_guide_update(&g,&in);CHECK(g.step==1); /* Stale green cannot pass. */
    in.guest_frame_serial=2;in.milliseconds=12000;in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(g.step==1);
    ctr_test_guide_view(&g,12000,view);CHECK(strstr(view[4],"NEW GREEN FRAME DETECTED")!=NULL);
    CHECK(strstr(guide_log_text,"A_to_green_ms=12000")!=NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==2);in.held=0;
    touches(&g,&in);CHECK(g.step==3);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==4);
    in.pressed=0;in.guest_sessions=1;in.guest_exited=true;in.guest_pass=true;
    ctr_test_guide_update(&g,&in);CHECK(g.step==5);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(g.step==5);
    in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.step==6 && ctr_test_guide_take_restart(&g));
    in.guest_exited=false;in.guest_pass=false;in.milliseconds=612000;ctr_test_guide_update(&g,&in);CHECK(g.step==7);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==8);
    ctr_test_guide_init(&g,CTR_GUIDE_INTERACTIVE,capture,NULL);in.pressed=0;in.guest_fault=true;
    ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
}

static void scale_tests(void) {
    const unsigned sizes[][5]={{1920,1080,1920,400,240},{854,480,896,320,240},
        {1280,720,1280,400,240},{31,19,37,7,5},{3,2,5,320,240},{7,11,12,1,1}};
    for(unsigned test=0;test<6;++test) {
        unsigned ow=sizes[test][0],oh=sizes[test][1],pitch=sizes[test][2],gw=sizes[test][3],gh=sizes[test][4];
        size_t pixels=(size_t)gw*gh,bytes=(size_t)pitch*oh*4+17;
        uint32_t *source=malloc(pixels*sizeof(*source));unsigned char *actual=malloc(bytes),*reference=calloc(bytes,1);
        CHECK(source && actual && reference);
        for(size_t i=0;i<pixels;++i)source[i]=(uint32_t)(i*UINT32_C(2654435761));
        CtrViewport v;CHECK(ctr_viewport_fit(&v,ow,oh,gw,gh));ctr_scale_plan plan={0};
        CHECK(ctr_scale_plan_init(&plan,&v));CHECK(!ctr_scale_plan_init(&plan,&v));
        CHECK(ctr_scale_rgba(&plan,source,pixels,actual,bytes,pitch));
        for(unsigned y=0;y<v.height;++y)for(unsigned x=0;x<v.width;++x) {
            uint32_t color=source[(size_t)((uint64_t)y*gh/v.height)*gw+(uint64_t)x*gw/v.width];
            size_t at=((size_t)(v.y+y)*pitch+v.x+x)*4;
            for(unsigned byte=0;byte<4;++byte)reference[at+byte]=(unsigned char)(color>>(24-byte*8));
        }
        CHECK(memcmp(actual,reference,bytes)==0); /* All pixels, borders, pitch padding and tail bytes. */
        CHECK(!ctr_scale_rgba(&plan,source,pixels-1,actual,bytes,pitch));
        CHECK(!ctr_scale_rgba(&plan,source,pixels,actual,(size_t)pitch*oh*4-1,pitch));
        CHECK(!ctr_scale_rgba(&plan,source,pixels,actual,bytes,ow-1));
        ctr_scale_plan_close(&plan);ctr_scale_plan_close(&plan);
        CHECK(!plan.columns && !plan.rows);
        CHECK(!ctr_scale_rgba(&plan,source,pixels,actual,bytes,pitch));
        free(source);free(actual);free(reference);
    }
    ctr_scale_plan plan={0};CtrViewport bad={1,0,1,1,1,1,1,1};
    CHECK(!ctr_scale_plan_init(&plan,&bad));bad.x=0;bad.guest_width=65536;
    CHECK(!ctr_scale_plan_init(&plan,&bad));bad.guest_width=1;bad.output_width=8193;
    CHECK(!ctr_scale_plan_init(&plan,&bad));CHECK(!ctr_scale_plan_init(NULL,&bad));
}

static void read_file(const char *path, char *buffer, size_t size) {
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    size_t n = fread(buffer, 1, size - 1, f);
    buffer[n] = 0;
    CHECK(!ferror(f));
    CHECK(fclose(f) == 0);
}

static void lifecycle_tests(void) {
    const uint64_t hbl[]={UINT64_C(0x0005000013374842),UINT64_C(0x000500101004a000),
        UINT64_C(0x000500101004a100),UINT64_C(0x000500101004a200)};
    for(unsigned i=0;i<4;++i) {
        ctr_test_lifecycle state={.title_id=hbl[i]};
        CHECK(ctr_test_from_hbl(state.title_id));
        CHECK(ctr_test_request_exit(&state)==CTR_EXIT_HBL);
        CHECK(ctr_test_request_exit(&state)==CTR_EXIT_NONE);
    }
    const uint64_t menu[]={0,UINT64_C(0x0005000013374843),UINT64_C(0x000500001011ec00)};
    for(unsigned i=0;i<3;++i) {
        ctr_test_lifecycle state={.title_id=menu[i]};
        CHECK(!ctr_test_from_hbl(state.title_id));
        CHECK(ctr_test_request_exit(&state)==CTR_EXIT_MENU);
        CHECK(state.requested);
        CHECK(ctr_test_request_exit(&state)==CTR_EXIT_NONE);
    }
    CHECK(ctr_test_request_exit(NULL)==CTR_EXIT_NONE);
}

typedef struct {
    const ctr_proc_status *states;
    unsigned count,polls,cleanups,menus,releases,waits,shutdowns,relaunches;
    bool proc_alive,graphics_alive,audio_alive,report_open,callbacks_cleared;
    char checkpoints[8192];
} fake_platform;
static void fake_cleanup(void *context) {
    fake_platform *p=context;
    CHECK(p->proc_alive && p->graphics_alive && p->audio_alive && p->report_open);
    CHECK(!p->menus && !p->cleanups);
    p->graphics_alive=p->audio_alive=p->report_open=false;++p->cleanups;
}
static void fake_clear(void *context) {
    fake_platform *p=context;
    CHECK(p->proc_alive && !p->graphics_alive && !p->report_open);
    p->callbacks_cleared=true;
}
static void fake_menu(void *context) {
    fake_platform *p=context;
    CHECK(p->proc_alive && p->callbacks_cleared && !p->graphics_alive && !p->audio_alive && !p->report_open);
    CHECK(!p->menus);++p->menus;
}
static ctr_proc_status fake_poll(void *context) {
    fake_platform *p=context;
    /* A callback attached to freed GFX, or polling after shutdown, is invalid. */
    CHECK(p->proc_alive && p->callbacks_cleared && p->menus==1);
    CHECK(p->polls<p->count);return p->states[p->polls++];
}
static void fake_release(void *context) {
    fake_platform *p=context;
    CHECK(p->polls && p->states[p->polls-1]==CTR_PROC_RELEASE);
    CHECK(!p->graphics_alive && !p->audio_alive);++p->releases;
}
static void fake_wait(void *context) {
    fake_platform *p=context;
    CHECK(p->polls && p->states[p->polls-1]!=CTR_PROC_EXITING);++p->waits;
}
static void fake_shutdown(void *context) {
    fake_platform *p=context;
    CHECK(p->proc_alive && !p->graphics_alive && !p->audio_alive && !p->report_open && p->callbacks_cleared);
    CHECK(!p->menus || (p->polls==p->count && p->states[p->polls-1]==CTR_PROC_EXITING));
    p->proc_alive=false;++p->shutdowns;
}
static void fake_whb(void *context) {
    fake_platform *p=context;
    CHECK(!p->proc_alive && p->shutdowns==1);++p->relaunches;
}
static void fake_checkpoint(void *context,const char *message) {
    fake_platform *p=context;size_t used=strlen(p->checkpoints),n=strlen(message);
    CHECK(!p->report_open && used+n+2<sizeof(p->checkpoints));
    memcpy(p->checkpoints+used,message,n);p->checkpoints[used+n]='\n';
}
static void exit_order_tests(void) {
    const ctr_exit_ops ops={fake_cleanup,fake_clear,fake_menu,fake_poll,fake_release,
        fake_wait,fake_shutdown,fake_whb,fake_checkpoint};
    const ctr_proc_status states[]={CTR_PROC_FOREGROUND,CTR_PROC_FOREGROUND,
        CTR_PROC_RELEASE,CTR_PROC_BACKGROUND,CTR_PROC_BACKGROUND,(ctr_proc_status)7,CTR_PROC_EXITING};
    fake_platform p={.states=states,.count=7,.proc_alive=true,.graphics_alive=true,.audio_alive=true,.report_open=true};
    ctr_test_lifecycle state={.title_id=UINT64_C(0x000500001011ec00)};
    CHECK(ctr_test_complete_exit(&state,false,&ops,&p));
    CHECK(state.finished && state.requested && p.cleanups==1 && p.menus==1 && p.polls==7);
    CHECK(p.releases==1 && p.waits==6 && p.shutdowns==1 && p.relaunches==1);
    CHECK(strstr(p.checkpoints,"EXIT status=BACKGROUND")!=NULL);
    CHECK(strstr(p.checkpoints,"EXIT status=UNRECOGNIZED")!=NULL);
    CHECK(strstr(p.checkpoints,"procui_shutdown_done")!=NULL);
    CHECK(ctr_test_complete_exit(&state,false,&ops,&p)); /* Repeated close does no platform work. */
    CHECK(p.cleanups==1 && p.menus==1 && p.polls==7 && p.shutdowns==1);
    const uint64_t ids[]={UINT64_C(0x0005000013374842),UINT64_C(0x000500101004a000),
        UINT64_C(0x000500101004a100),UINT64_C(0x000500101004a200)};
    for(unsigned i=0;i<4;++i) {
        memset(&p,0,sizeof(p));p.proc_alive=p.graphics_alive=p.audio_alive=p.report_open=true;
        state=(ctr_test_lifecycle){.title_id=ids[i]};
        CHECK(ctr_test_complete_exit(&state,false,&ops,&p));
        CHECK(p.cleanups==1 && !p.menus && !p.polls && p.shutdowns==1 && p.relaunches==1);
    }
    memset(&p,0,sizeof(p));p.proc_alive=p.graphics_alive=p.audio_alive=p.report_open=true;
    state=(ctr_test_lifecycle){.title_id=UINT64_C(0x000500001011ec00)};
    CHECK(ctr_test_complete_exit(&state,true,&ops,&p)); /* System already requested exit. */
    CHECK(!p.menus && !p.polls && p.cleanups==1 && p.shutdowns==1);
    CHECK(!ctr_test_complete_exit(NULL,true,&ops,&p));
    CHECK(!ctr_test_complete_exit(&state,true,NULL,&p));
    ctr_exit_ops bad=ops;bad.shutdown_procui=NULL;
    state=(ctr_test_lifecycle){0};
    CHECK(!ctr_test_complete_exit(&state,false,&bad,&p));
    CHECK(!state.requested && !state.finished);
}

int main(void) {
    guide_tests();
    lifecycle_tests();
    exit_order_tests();
    scale_tests();
    char directory[] = "/tmp/ctr2wup-report-XXXXXX", text[4096];
    CHECK(mkdtemp(directory) != NULL);
    ctr_test_report first = {0}, second = {0}, bad = {0};
    CHECK(ctr_test_report_open(&first, directory, "native", "0.14.1", 1));
    CHECK(strstr(first.filename, "native-0000000000000001-00.txt") != NULL);
    read_file(first.path, text, sizeof(text));
    CHECK(strstr(text, "Session STARTED") != NULL);
    CHECK(strstr(text, "interrupted/incomplete evidence") != NULL);
    CHECK(ctr_test_report_line(&first, "CPU_selftest PASS result=0"));
    read_file(first.path, text, sizeof(text));
    CHECK(strstr(text, "CPU_selftest PASS result=0") != NULL); /* Visible before close. */
    CHECK(ctr_test_report_open(&second, directory, "native", "0.14.1", 1));
    CHECK(strcmp(first.path, second.path) != 0);
    CHECK(strstr(second.filename, "-01.txt") != NULL);
    CHECK(ctr_test_report_line(&second, "SAVE LOAD / MOUNT FAIL"));
    CHECK(ctr_test_report_close(&second));
    read_file(first.path, text, sizeof(text));
    CHECK(strstr(text, "CPU_selftest PASS result=0") != NULL);
    CHECK(strstr(text, "SAVE LOAD / MOUNT FAIL") == NULL); /* A second run cannot replace it. */
    CHECK(ctr_test_report_line(&first, "===== SESSION SUMMARY ====="));
    CHECK(ctr_test_report_line(&first, "Cleanup reached: yes | result=0"));
    CHECK(ctr_test_report_close(&first));
    CHECK(ctr_test_report_close(&first));
    CHECK(ctr_test_report_checkpoint(&first,"EXIT stage=report_close_done"));
    CHECK(first.file==NULL);
    read_file(first.path,text,sizeof(text));
    CHECK(strstr(text,"CPU_selftest PASS result=0")!=NULL);
    CHECK(strstr(text,"EXIT stage=report_close_done")!=NULL);
    read_file(second.path, text, sizeof(text));
    CHECK(strstr(text, "SAVE LOAD / MOUNT FAIL") != NULL);
    CHECK(strstr(text, "===== SESSION SUMMARY =====") == NULL); /* Interrupted evidence is not a pass. */
    CHECK(!ctr_test_report_open(&bad, "/path-that-does-not-exist/ctr2wup", "native", "0.14.1", 1));
    CHECK(bad.failed && bad.file == NULL && bad.error_code != 0);
    CHECK(!ctr_test_report_open(&bad, directory, "../escape", "0.14.1", 1));
    CHECK(bad.error_code == EINVAL);
    char long_directory[600]; memset(long_directory, 'a', sizeof(long_directory)-1); long_directory[599] = 0;
    CHECK(!ctr_test_report_open(&bad, long_directory, "native", "0.14.1", 1));
    CHECK(bad.error_code == ENAMETOOLONG);
    ctr_test_report full = {0};
    full.file = fopen("/dev/full", "w"); CHECK(full.file != NULL);
    CHECK(!ctr_test_report_line(&full, "A write failure must stay visible"));
    CHECK(full.failed && full.error_code != 0);
    CHECK(!ctr_test_report_line(&full, "Never clear the failed status"));
    CHECK(!ctr_test_report_close(&full));
    CHECK(full.file == NULL);
    CHECK(!ctr_test_report_checkpoint(&full,"Must not hide the prior SD error"));
    CHECK(full.file == NULL);
    CHECK(unlink(second.path)==0);
    CHECK(!ctr_test_report_checkpoint(&second,"Never recreate a missing result"));
    CHECK(second.failed && second.error_code==ENOENT);
    CHECK(access(second.path,F_OK)!=0);
    ctr_test_report busy={0};
    CHECK(ctr_test_report_open(&busy,directory,"native","0.14.2",2));
    CHECK(!ctr_test_report_checkpoint(&busy,"Reject an active stream"));
    CHECK(busy.failed && busy.error_code==EBUSY);
    CHECK(!ctr_test_report_close(&busy));
    CHECK(busy.file==NULL);
    CHECK(unlink(busy.path)==0);
    CHECK(unlink(first.path) == 0); CHECK(rmdir(directory) == 0);
    printf("{\"ok\":true,\"assertions\":%u,\"text_reports\":true,\"console_verified\":false}\n", checks);
    return 0;
}
