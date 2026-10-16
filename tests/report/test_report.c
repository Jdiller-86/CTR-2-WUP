#define _POSIX_C_SOURCE 200809L
#include "test_report.h"
#include "test_result.h"
#include "report_storage.h"
#include "test_guide.h"
#include "test_lifecycle.h"
#include "image_scale.h"
#include "guest_slice.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../test_platform.h"
#include <coreinit/filesystem_fsa.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %u line %d: %s\n", checks, __LINE__, #x); exit(1); } } while (0)

static int32_t storage_add_result=123,storage_flush_result,storage_delete_result;
static unsigned storage_add_calls,storage_flush_calls,storage_delete_calls;
FSAClientHandle FSAAddClient(FSAClientAttachAsyncData *data) {
    CHECK(data==NULL);++storage_add_calls;return storage_add_result;
}
FSError FSAFlushVolume(FSAClientHandle client,const char *path) {
    CHECK(client==123 && strcmp(path,"/vol/external01")==0);
    ++storage_flush_calls;return storage_flush_result;
}
FSError FSADelClient(FSAClientHandle client) {
    CHECK(client==123);++storage_delete_calls;return storage_delete_result;
}
static void storage_tests(void) {
    ctr_report_storage storage={.client=-1};
    CHECK(ctr_report_storage_sync(&storage));
    CHECK(storage.client==123 && storage.last_status==0 && storage_add_calls==1 && storage_flush_calls==1);
    CHECK(ctr_report_storage_sync(&storage));
    CHECK(storage_add_calls==1 && storage_flush_calls==2); /* Reuse only this helper's client. */
    storage_flush_result=-196665;
    CHECK(!ctr_report_storage_sync(&storage));
    CHECK(errno==EIO && storage.last_status==-196665 && storage.client==123);
    ctr_report_storage_close(&storage);
    CHECK(storage.client==-1 && storage_delete_calls==1 && storage.last_status==0);
    ctr_report_storage_close(&storage);CHECK(storage_delete_calls==1);
    storage_add_result=-7;
    CHECK(!ctr_report_storage_sync(&storage));
    CHECK(errno==EIO && storage.last_status==-7 && storage_flush_calls==3);
    storage_add_result=0;
    CHECK(!ctr_report_storage_sync(&storage));
    CHECK(errno==EIO && storage.last_status==0 && storage_flush_calls==3);
    CHECK(!ctr_report_storage_sync(NULL) && errno==EINVAL);
    ctr_report_storage_close(NULL);
    storage_add_result=123;storage_flush_result=0;
    CHECK(ctr_report_storage_sync(&storage));
    storage_delete_result=-17;ctr_report_storage_close(&storage);
    CHECK(storage.client==-1 && storage.last_status==-17 && storage_delete_calls==2);
}

static char guide_log_text[32768];
typedef struct { uint64_t now,cost;unsigned calls,mode; } slice_fixture;
static uint64_t fake_clock(void *context) { return ((slice_fixture *)context)->now; }
static ctr_slice_step fake_execute(void *context,unsigned limit) {
    slice_fixture *f=context;++f->calls;
    f->now=f->mode==6?f->now-1:f->now+f->cost;
    ctr_slice_step s={limit,CTR_SLICE_RUN,false};
    if(f->mode==1 && f->calls==2)s.frame=true;
    if(f->mode==2) { s.instructions=0;s.state=CTR_SLICE_WAIT; }
    if(f->mode==3) { s.instructions=0;s.state=CTR_SLICE_YIELD; }
    if(f->mode==4)s.state=CTR_SLICE_STOP;
    if(f->mode==5)s.instructions=0;
    if(f->mode==7)s.instructions=limit+1u;
    if(f->mode==8)s.state=(ctr_slice_state)-1;
    return s;
}
static void slice_tests(void) {
    ctr_slice_policy p={10,50,500,8000};slice_fixture f={100,2000,0,0};
    ctr_slice_report r=ctr_guest_slice(&p,fake_clock,fake_execute,&f);
    CHECK(r.reason==CTR_SLICE_TIME && r.calls==4 && r.instructions==40 && r.elapsed_us==8000);
    p.max_instructions=25;p.max_us=100000;f=(slice_fixture){100,1,0,0};
    r=ctr_guest_slice(&p,fake_clock,fake_execute,&f);
    CHECK(r.reason==CTR_SLICE_INSTRUCTIONS && r.calls==3 && r.instructions==25);
    p.max_instructions=500;p.max_calls=2;f=(slice_fixture){100,1,0,0};
    r=ctr_guest_slice(&p,fake_clock,fake_execute,&f);CHECK(r.reason==CTR_SLICE_CALLS && r.calls==2);
    const ctr_slice_reason expected[]={CTR_SLICE_FRAME,CTR_SLICE_WAITING,CTR_SLICE_YIELDED,
        CTR_SLICE_STOPPED,CTR_SLICE_NO_PROGRESS,CTR_SLICE_CLOCK,CTR_SLICE_CONTRACT,CTR_SLICE_CONTRACT};
    for(unsigned mode=1;mode<=8;++mode) {
        f=(slice_fixture){100,1,0,mode};r=ctr_guest_slice(&p,fake_clock,fake_execute,&f);
        CHECK(r.reason==expected[mode-1] && r.calls==(mode==1?2u:1u));
    }
    p.max_calls=50;p.max_us=1;f=(slice_fixture){100,20000,0,0};
    r=ctr_guest_slice(&p,fake_clock,fake_execute,&f);CHECK(r.reason==CTR_SLICE_TIME && r.calls==1 && r.elapsed_us==20000);
    CHECK(ctr_guest_slice(NULL,fake_clock,fake_execute,&f).reason==CTR_SLICE_ARGUMENT);
    p.quantum=0;CHECK(ctr_guest_slice(&p,fake_clock,fake_execute,&f).reason==CTR_SLICE_ARGUMENT);
    CHECK(!ctr_draw_observation_ready(true,1,4,4,97200,36));
    CHECK(!ctr_draw_observation_ready(false,2,4,4,97200,36));
    CHECK(!ctr_draw_observation_ready(true,2,3,4,97200,36));
    CHECK(!ctr_draw_observation_ready(true,2,4,3,97200,36));
    CHECK(!ctr_draw_observation_ready(true,2,4,4,97199,36));
    CHECK(!ctr_draw_observation_ready(true,2,4,4,97200,35));
    CHECK(ctr_draw_observation_ready(true,2,4,4,97200,36));
}
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
    ctr_test_guide_init(&g,CTR_GUIDE_ASSETS,capture,NULL);
    in.guest_exited=true;ctr_test_guide_update(&g,&in);CHECK(g.done && g.failed==1 && !g.passed);
    ctr_test_guide_init(&g,CTR_GUIDE_ASSETS,capture,NULL);
    in.guest_pass=true;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==1 && !g.failed);
    ctr_test_guide_init(&g,CTR_GUIDE_GX,capture,NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(!g.step);
    in.guest_frame_available=true;ctr_test_guide_update(&g,&in);CHECK(g.step==1 && g.passed==1);
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==2);
    memset(&in,0,sizeof(in));
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
    ctr_test_guide_view(&g,in.milliseconds,view);CHECK(strstr(view[3],"SD:/CTR2WUP/RESULTS/")!=NULL);
    CHECK(strstr(view[4],"ZIP and return")!=NULL);
    CHECK(strstr(guide_log_text,"User confirmed")!=NULL && strstr(guide_log_text,"ten requested buttons")!=NULL);
    ctr_test_guide_summary(&g);CHECK(strstr(guide_log_text,"not_finished=0")!=NULL);
    char results[1536];
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"1. Picture check: PASS | User confirmed")!=NULL);
    CHECK(strstr(results,"2. Buttons: PASS | All ten requested buttons observed")!=NULL);
    CHECK(strstr(results,"10. Stability observation: PASS")!=NULL);
    CHECK(strstr(results,"NOT TESTED")==NULL);
    CHECK(!ctr_test_guide_results(&g,results,8));

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_NATIVE,capture,NULL);
    in.pressed=CTR_GUIDE_L;ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
    in.pressed=CTR_GUIDE_SKIP;ctr_test_guide_update(&g,&in);CHECK(g.skipped==1 && g.step==2);
    in.pressed=CTR_GUIDE_MINUS;ctr_test_guide_update(&g,&in);CHECK(g.cancelled && !g.done);
    ctr_test_guide_summary(&g);CHECK(strstr(guide_log_text,"NOT TESTED")!=NULL && strstr(guide_log_text,"not_finished=8")!=NULL);
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"1. Picture check: FAIL | User reported")!=NULL);
    CHECK(strstr(results,"2. Buttons: NOT TESTED | User pressed ZL to skip")!=NULL);
    CHECK(strstr(results,"3. Left stick: NOT TESTED | Step not completed")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_SAVE,capture,NULL);
    in.guest_sessions=1;in.guest_exited=true;in.guest_pass=true;
    ctr_test_guide_update(&g,&in);CHECK(g.step==1);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    CHECK(!ctr_test_guide_take_restart(&g));
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(!g.done); /* Old process PASS does not establish reload. */
    in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==2);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[6],"BOTH reports")!=NULL);
    CHECK(strstr(view[0],"FRESH LAUNCH STILL NEEDED")!=NULL);
    CHECK(strstr(view[2],"open this SAME app again")!=NULL);
    ctr_test_guide_init(&g,CTR_GUIDE_SAVE,capture,NULL);in.guest_sessions=1;in.fresh_save_reload=true;
    ctr_test_guide_update(&g,&in);CHECK(g.fresh_reload_confirmed);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    in.pressed=0;in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.done);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[6],"testing complete")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_AUDIO,capture,NULL);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[1],"Launch music stopped")!=NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==1 && g.passed==1);
    ctr_test_guide_update(&g,&in);CHECK(!g.done); /* Hearing launch music cannot stand in for requesting the original tone. */
    in.pressed=CTR_GUIDE_A;ctr_test_guide_update(&g,&in);CHECK(!g.done);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==2 && !g.failed);
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"Launch music stopped: PASS")!=NULL);
    CHECK(strstr(results,"Native test tone: PASS")!=NULL);

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_SCENE,capture,NULL);
    ctr_test_guide_update(&g,&in);CHECK(!g.done && !g.passed);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(!g.step);
    in.guest_frame_available=true;ctr_test_guide_update(&g,&in);CHECK(g.step==1 && g.passed==1);
    ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==2);
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"Arithmetic triangle picture: PASS") && strstr(results,"Shared diagonal: PASS"));
    ctr_test_guide_init(&g,CTR_GUIDE_SCENE,capture,NULL);in.guest_fault=true;
    ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
    ctr_test_guide_update(&g,&in);CHECK(g.done && g.failed==2);
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"Automatic scene test failed"));

    guide_log_text[0]=0;memset(&in,0,sizeof(in));
    ctr_test_guide_init(&g,CTR_GUIDE_PICA,capture,NULL);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==0);
    in.guest_frame_available=true;in.guest_frame_serial=1;ctr_test_guide_update(&g,&in);CHECK(g.step==0);
    ctr_test_guide_view(&g,0,view);CHECK(strstr(view[4],"startup image")!=NULL);
    in.guest_drawn_frame_available=true;in.guest_frame_serial=2;ctr_test_guide_update(&g,&in);CHECK(g.step==1 && g.passed==1);
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(!g.done);
    in.guest_exited=true;in.guest_pass=true;ctr_test_guide_update(&g,&in);CHECK(!g.done && g.passed==2);
    in.guest_sessions=1;in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(!g.done);
    in.guest_sessions=2;in.guest_drawn_frame_available=false;ctr_test_guide_update(&g,&in);CHECK(!g.done);
    in.guest_drawn_frame_available=true;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==3);
    CHECK(ctr_test_guide_results(&g,results,sizeof(results)));
    CHECK(strstr(results,"Guest triangle pictures: PASS") && strstr(results,"Guest draws / readback / exit: PASS"));

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

static void scale_reference(const CtrViewport *v,const uint32_t *source,unsigned char *out,size_t bytes,unsigned pitch) {
    memset(out,0,bytes);
    for(unsigned y=0;y<v->height;++y)for(unsigned x=0;x<v->width;++x) {
        uint32_t color=source[(size_t)((uint64_t)y*v->guest_height/v->height)*v->guest_width+
            (uint64_t)x*v->guest_width/v->width];
        size_t at=((size_t)(v->y+y)*pitch+v->x+x)*4;
        for(unsigned byte=0;byte<4;++byte)out[at+byte]=(unsigned char)(color>>(24-byte*8));
    }
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
        scale_reference(&v,source,reference,bytes,pitch);
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

static void cache_damage_check(const ctr_scale_cache *c,const ctr_scale_update *u,
                               const unsigned char *before,const unsigned char *after,size_t bytes) {
    uint64_t total=0;size_t previous_end=0;bool bounded=true,covered=true;
    for(size_t n=0;n<c->range_count;++n) {
        ctr_scale_range range=c->ranges[n];
        if(!range.bytes || range.offset>bytes || range.bytes>bytes-range.offset ||
           (n && range.offset<=previous_end))bounded=false;
        previous_end=range.offset+range.bytes;total+=range.bytes;
    }
    size_t n=0;
    for(size_t at=0;at<bytes;++at)if(before[at]!=after[at]) {
        while(n<c->range_count && at>=c->ranges[n].offset+c->ranges[n].bytes)++n;
        if(n==c->range_count || at<c->ranges[n].offset) { covered=false;break; }
    }
    CHECK(bounded);CHECK(covered);CHECK(total==u->publish_bytes);
    CHECK(c->range_count<=c->range_capacity);
}
static void scale_cache_tests(void) {
    const unsigned sizes[][5]={{1920,1080,1920,400,240},{854,480,896,320,240},
        {1280,720,1280,400,240},{31,19,37,7,5},{3,2,5,320,240},{7,11,12,1,1}};
    uint32_t random=123456789;
    for(unsigned test=0;test<6;++test) {
        unsigned ow=sizes[test][0],oh=sizes[test][1],pitch=sizes[test][2],gw=sizes[test][3],gh=sizes[test][4];
        size_t pixels=(size_t)gw*gh,bytes=(size_t)(pitch+2)*oh*4+17;
        uint32_t *source=malloc(pixels*4),*history=malloc(pixels*4);
        unsigned char *out=malloc(bytes),*other=malloc(bytes),*before=malloc(bytes),*reference=malloc(bytes);
        CHECK(source && history && out && other && before && reference);
        for(size_t i=0;i<pixels;++i)source[i]=(uint32_t)(i*UINT32_C(2654435761));
        memset(out,0xa5,bytes);memset(other,0x5a,bytes);
        CtrViewport view;ctr_scale_plan plan={0};ctr_scale_cache cache={0};ctr_scale_update update={0};
        CHECK(ctr_viewport_fit(&view,ow,oh,gw,gh));CHECK(ctr_scale_plan_init(&plan,&view));
        CHECK(ctr_scale_cache_init(&cache,&plan));CHECK(!ctr_scale_cache_init(&cache,&plan));
        for(unsigned frame=0;frame<14;++frame) {
            if(frame==12) {
                for(size_t i=0;i<pixels;++i)source[i]^=UINT32_C(0xffffffff);
            } else if(frame==13 && test==4) {
                /* Neither row 1 nor column 1 is sampled by the 3x2 downscale. */
                source[gw+1]^=UINT32_C(0x11223344);
            } else if(frame>1 && frame<12) {
                for(unsigned mutation=0;mutation<1+frame*3;++mutation) {
                    random=random*1664525u+1013904223u;
                    source[random%pixels]^=random|1u;
                }
                source[0]^=0x11223344u;source[pixels-1]^=0x89abcdefu;
            }
            memcpy(before,out,bytes);
            CHECK(ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,&update));
            scale_reference(&view,source,reference,bytes,pitch);
            CHECK(!memcmp(out,reference,bytes));cache_damage_check(&cache,&update,before,out,bytes);
            CHECK(update.full==(frame==0));
            if(frame==0)CHECK(cache.range_count==1 && update.publish_bytes==bytes);
            if(frame==1)CHECK(!cache.range_count && !update.publish_bytes && !update.scaled_pixels);
            if(frame==12)CHECK(update.scaled_pixels==(uint64_t)view.width*view.height);
            if(frame==13)CHECK(!cache.range_count && !update.publish_bytes);
        }
        memcpy(history,cache.previous,pixels*4);memcpy(before,out,bytes);
        size_t old_ranges=cache.range_count;ctr_scale_update unchanged=update;
        CHECK(!ctr_scale_cache_update(&cache,source,pixels-1,out,bytes,pitch,&update));
        CHECK(!ctr_scale_cache_update(&cache,source,pixels,out,(size_t)pitch*oh*4-1,pitch,&update));
        CHECK(!ctr_scale_cache_update(&cache,source,pixels,out,bytes,ow-1,&update));
        CHECK(!ctr_scale_cache_update(&cache,(uint32_t *)out,pixels,out,bytes,pitch,&update));
        CHECK(!ctr_scale_cache_update(&cache,cache.previous,pixels,out,bytes,pitch,&update));
        CHECK(!ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,NULL));
        CHECK(!ctr_scale_cache_update(&cache,source,pixels,(unsigned char *)(UINTPTR_MAX-8),bytes,pitch,&update));
        plan.view.width++;
        CHECK(!ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,&update));plan.view.width--;
        CHECK(!memcmp(history,cache.previous,pixels*4));CHECK(!memcmp(before,out,bytes));
        CHECK(unchanged.full==update.full && unchanged.source_rows==update.source_rows &&
              unchanged.output_rows==update.output_rows && unchanged.scaled_pixels==update.scaled_pixels &&
              unchanged.publish_bytes==update.publish_bytes);CHECK(cache.range_count==old_ranges);
        CHECK(ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,&update));
        CHECK(!cache.range_count && !update.publish_bytes);
        ctr_scale_cache_invalidate(&cache);memset(out,0xa5,bytes);
        CHECK(ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,&update));
        CHECK(update.full);CHECK(!memcmp(out,reference,bytes));
        CHECK(ctr_scale_cache_update(&cache,source,pixels,other,bytes,pitch,&update));
        CHECK(update.full);CHECK(!memcmp(other,reference,bytes));
        CHECK(ctr_scale_cache_update(&cache,source,pixels,other,bytes,pitch+1,&update));
        scale_reference(&view,source,reference,bytes,pitch+1);CHECK(update.full);CHECK(!memcmp(other,reference,bytes));
        CHECK(ctr_scale_cache_update(&cache,source,pixels,other,bytes-1,pitch+1,&update));
        scale_reference(&view,source,reference,bytes-1,pitch+1);CHECK(update.full);CHECK(!memcmp(other,reference,bytes-1));
        ctr_scale_cache_close(&cache);ctr_scale_cache_close(&cache);
        CHECK(!cache.previous && !cache.ranges);CHECK(!ctr_scale_cache_update(&cache,source,pixels,out,bytes,pitch,&update));
        ctr_scale_plan bad=plan;bad.view.guest_width=1025;bad.view.guest_height=1025;
        CHECK(!ctr_scale_cache_init(&cache,&bad));CHECK(!ctr_scale_cache_init(NULL,&plan));
        ctr_scale_plan_close(&plan);free(source);free(history);free(out);free(other);free(before);free(reference);
    }
    ctr_scale_cache_close(NULL);ctr_scale_cache_invalidate(NULL);
}

static void read_file(const char *path, char *buffer, size_t size) {
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    size_t n = fread(buffer, 1, size - 1, f);
    buffer[n] = 0;
    CHECK(!ferror(f));
    CHECK(fclose(f) == 0);
}

static void return_directory_tests(void) {
    char root[384],directory[512],snapshot[512],romfs[512],text[256];
    CHECK(ctr_test_temp_directory(root,sizeof(root),"ctr2wup-return"));
    snprintf(snapshot,sizeof(snapshot),"%s/filesystem-test.snapshot",root);
    snprintf(romfs,sizeof(romfs),"%s/romfs-test.bin",root);
    const char *inputs[]={snapshot,romfs};
    for(unsigned i=0;i<2;++i) {
        FILE *file=fopen(inputs[i],"wb");CHECK(file!=NULL);
        CHECK(fputs("Original data stays outside the return folder",file)>=0);
        CHECK(fclose(file)==0);
    }
    for(unsigned install=0;install<2;++install) {
        CHECK(ctr_test_report_directory(root,install!=0,directory,sizeof(directory)));
        CHECK(ctr_test_report_directory(root,install!=0,directory,sizeof(directory))); /* Repeated launch. */
        ctr_test_report trace={0};ctr_test_result result={0};
        CHECK(ctr_test_report_open(&trace,directory,"native","0.27.0",1));
        CHECK(ctr_test_result_open(&result,directory,"native","0.27.0",1));
        CHECK(ctr_test_report_line(&trace,"Original trace"));
        CHECK(ctr_test_report_close(&trace));
        CHECK(ctr_test_result_update(&result,"Original result"));
        CHECK(strncmp(trace.path,directory,strlen(directory))==0);
        CHECK(strncmp(result.path,directory,strlen(directory))==0);
        for(unsigned i=0;i<2;++i) {
            read_file(inputs[i],text,sizeof(text));
            CHECK(strcmp(text,"Original data stays outside the return folder")==0);
        }
        CHECK(ctr_test_unlink(trace.path)==0 && ctr_test_unlink(result.path)==0);
        CHECK(ctr_test_remove_directory(directory)==0); /* Only the two TXT files were created. */
    }
    CHECK(ctr_test_report_directory(root,false,directory,sizeof(directory)));
    CHECK(ctr_test_remove_directory(directory)==0);
    FILE *blocked=fopen(directory,"wb");CHECK(blocked!=NULL && fclose(blocked)==0);
    CHECK(!ctr_test_report_directory(root,false,directory,sizeof(directory)) && errno==ENOTDIR);
    CHECK(ctr_test_unlink(directory)==0);
    CHECK(!ctr_test_report_directory(root,false,directory,4) && errno==ENAMETOOLONG);
    for(unsigned i=0;i<2;++i)CHECK(ctr_test_unlink(inputs[i])==0);
    CHECK(ctr_test_remove_directory(root)==0);
}

typedef struct {
    ctr_test_report *report;
    unsigned calls;
    int failure;
    bool remove_file;
} sync_probe;

static bool fake_volume_sync(void *context) {
    sync_probe *probe=context;
    CHECK(probe->report->file==NULL); /* Metadata sync must follow a checked close. */
    ++probe->calls;
    if(probe->remove_file)CHECK(ctr_test_unlink(probe->report->path)==0);
    if(probe->failure) { errno=probe->failure;return false; }
    return true;
}

static int interrupted_report(const char *mode,const char *directory) {
    if(!strcmp(mode,"report-interrupt")) {
        ctr_test_report interrupted={0};
        if(!ctr_test_report_open(&interrupted,directory,"compiled-guarded","0.14.5",13) ||
           !ctr_test_report_line(&interrupted,"INPUT CHECKPOINT stage=compiled_guest_begin") ||
           !ctr_test_report_commit(&interrupted))ctr_test_crash_exit(1);
        /* Control buffering explicitly: CRT default buffering differs by platform.
           Set the buffer before any operation on this new append stream. */
        if(fclose(interrupted.file))ctr_test_crash_exit(1);
        interrupted.file=fopen(interrupted.path,"ab");
        static char buffer[4096];
        if(!interrupted.file || setvbuf(interrupted.file,buffer,_IOFBF,sizeof(buffer)))ctr_test_crash_exit(1);
        if(fputs("UNCOMMITTED BUFFER",interrupted.file)<0)ctr_test_crash_exit(2);
        ctr_test_crash_exit(77);
    }
    if(!strcmp(mode,"result-interrupt")) {
        ctr_test_result interrupted={0};
        if(!ctr_test_result_open(&interrupted,directory,"native","0.14.6",101) ||
           !ctr_test_result_update(&interrupted,"Stage: compiled_guest_begin\nGuide: RUNNING\n"))ctr_test_crash_exit(1);
        ctr_test_crash_exit(77);
    }
    return 90;
}

static void commit_tests(const char *directory) {
    char text[4096];ctr_test_report r={0};sync_probe probe={.report=&r};
    CHECK(ctr_test_report_open(&r,directory,"compiled-guarded","0.14.5",10));
    ctr_test_report_set_volume_sync(&r,fake_volume_sync,&probe);
    CHECK(ctr_test_report_line(&r,"INPUT CHECKPOINT stage=compiled_guest_begin"));
    CHECK(ctr_test_report_commit(&r));
    CHECK(probe.calls==1 && r.commits==1 && r.file!=NULL);
    read_file(r.path,text,sizeof(text));
    CHECK(strstr(text,"stage=compiled_guest_begin")!=NULL);
    CHECK(ctr_test_report_line(&r,"INPUT CHECKPOINT stage=compiled_guest_done"));
    CHECK(ctr_test_report_commit(&r));
    read_file(r.path,text,sizeof(text));
    CHECK(strstr(text,"stage=compiled_guest_begin")!=NULL && strstr(text,"stage=compiled_guest_done")!=NULL);
    CHECK(probe.calls==2 && r.commits==2);
    CHECK(ctr_test_report_close(&r));
    CHECK(probe.calls==3 && r.commits==3 && r.file==NULL);
    CHECK(ctr_test_report_checkpoint(&r,"EXIT stage=main_return"));
    CHECK(probe.calls==4 && r.commits==4 && r.file==NULL);
    CHECK(ctr_test_unlink(r.path)==0);

    r=(ctr_test_report){0};probe=(sync_probe){.report=&r,.failure=ENOSPC};
    CHECK(ctr_test_report_open(&r,directory,"compiled-guarded","0.14.5",11));
    ctr_test_report_set_volume_sync(&r,fake_volume_sync,&probe);
    CHECK(!ctr_test_report_commit(&r));
    CHECK(r.failed && r.error_code==ENOSPC && strcmp(r.error_stage,"volume_sync")==0);
    CHECK(!r.file && !r.commits && probe.calls==1);
    CHECK(!ctr_test_report_line(&r,"Never erase the first failure"));
    CHECK(!ctr_test_report_close(&r) && !ctr_test_report_commit(&r));
    CHECK(r.error_code==ENOSPC && strcmp(r.error_stage,"volume_sync")==0 && probe.calls==1);
    CHECK(ctr_test_unlink(r.path)==0);

    r=(ctr_test_report){0};probe=(sync_probe){.report=&r,.remove_file=true};
    CHECK(ctr_test_report_open(&r,directory,"compiled-guarded","0.14.5",12));
    ctr_test_report_set_volume_sync(&r,fake_volume_sync,&probe);
    CHECK(!ctr_test_report_commit(&r));
    CHECK(r.failed && !r.file && r.error_code==ENOENT && strcmp(r.error_stage,"reopen")==0);
    CHECK(ctr_test_access(r.path,F_OK)!=0); /* Missing committed reports cannot be recreated. */

    int pipe_fd[2];CHECK(ctr_test_pipe(pipe_fd)==0);
    r=(ctr_test_report){0};r.file=ctr_test_fdopen(pipe_fd[1],"w");CHECK(r.file!=NULL);
    probe=(sync_probe){.report=&r};ctr_test_report_set_volume_sync(&r,fake_volume_sync,&probe);
    CHECK(!ctr_test_report_commit(&r)); /* A pipe cannot acknowledge file sync. */
    CHECK(!r.file && r.failed && r.error_code==EINVAL && strcmp(r.error_stage,"file_sync")==0);
    CHECK(!probe.calls && !r.commits);
    CHECK(ctr_test_close(pipe_fd[0])==0);
    CHECK(!ctr_test_report_commit(NULL));

    char path[512];CHECK(snprintf(path,sizeof(path),"%s/compiled-guarded-%016llx-00.txt",directory,13ULL)>0);
    ctr_test_child child;CHECK(ctr_test_child_start(&child,"report-interrupt",directory,interrupted_report));
    unsigned status;CHECK(ctr_test_child_wait(&child,&status));CHECK(status==77);
    read_file(path,text,sizeof(text));
    CHECK(strstr(text,"stage=compiled_guest_begin")!=NULL);
    CHECK(strstr(text,"UNCOMMITTED BUFFER")==NULL && strstr(text,"===== END OF REPORT:")==NULL);
    CHECK(ctr_test_unlink(path)==0);
}
static void result_tests(const char *directory) {
    ctr_test_result first={0},second={0};char text[CTR_TEST_RESULT_BYTES+2];
    CHECK(ctr_test_result_open(&first,directory,"compiled-save","0.14.6",100));
    CHECK(first.ready && !first.failed && first.verified_writes==1);
    CHECK(strstr(first.filename,"result-compiled-save-0.14.6-")!=NULL);
    read_file(first.path,text,sizeof(text));CHECK(strlen(text)==CTR_TEST_RESULT_BYTES);
    CHECK(strstr(text,"Build: 0.14.6") && strstr(text,"Session: STARTED"));
    CHECK(ctr_test_result_update(&first,"Guide: COMPLETE | passed=2 failed=0\nGuest: exited\n"));
    CHECK(first.verified_writes==2);
    read_file(first.path,text,sizeof(text));CHECK(strstr(text,"passed=2 failed=0") && strstr(text,"Power-loss recovery unverified"));
    CHECK(ctr_test_result_open(&second,directory,"compiled-save","0.14.6",100));
    CHECK(strcmp(first.path,second.path)!=0 && strcmp(first.identity,second.identity)!=0);
    CHECK(ctr_test_result_update(&first,"Stage: input_read\n"));
    read_file(first.path,text,sizeof(text));CHECK(strstr(text,"passed=2 failed=0")==NULL); /* Full fixed-size replacement, no stale tail. */
    char other[CTR_TEST_RESULT_BYTES+1];read_file(second.path,other,sizeof(other));
    FILE *file=fopen(first.path,"wb");CHECK(file && fwrite(other,1,CTR_TEST_RESULT_BYTES,file)==CTR_TEST_RESULT_BYTES && fclose(file)==0);
    CHECK(!ctr_test_result_update(&first,"Never overwrite another reserved result"));
    CHECK(first.failed && first.error_code==EIO && strcmp(first.error_stage,"read_verify")==0);
    read_file(first.path,text,sizeof(text));CHECK(memcmp(text,other,CTR_TEST_RESULT_BYTES)==0);
    CHECK(!ctr_test_result_update(&first,"Sticky failure"));CHECK(ctr_test_unlink(first.path)==0);
    CHECK(ctr_test_unlink(second.path)==0);
    CHECK(!ctr_test_result_update(&second,"Do not recreate a missing receipt"));
    CHECK(second.failed && second.error_code==ENOENT && ctr_test_access(second.path,F_OK)!=0);
    ctr_test_result bad={0};CHECK(!ctr_test_result_open(&bad,directory,"../escape","0.14.6",1) && bad.error_code==EINVAL);
    CHECK(!ctr_test_result_open(&bad,"/missing/ctr2wup-results","native","0.14.6",1));
    CHECK(!ctr_test_result_open(NULL,directory,"native","0.14.6",1));
    CHECK(!ctr_test_result_update(NULL,"body"));
    char path[512];CHECK(snprintf(path,sizeof(path),"%s/result-native-0.14.6-%016llx-00.txt",directory,101ULL)>0);
    ctr_test_child child;CHECK(ctr_test_child_start(&child,"result-interrupt",directory,interrupted_report));
    unsigned status;CHECK(ctr_test_child_wait(&child,&status) && status==77);
    read_file(path,text,sizeof(text));CHECK(strstr(text,"compiled_guest_begin") && strstr(text,"Guide: RUNNING"));
    CHECK(ctr_test_unlink(path)==0);
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

unsigned test_audio_stream_app(void);
int main(int argc,char **argv) {
    if(argc==4 && !strcmp(argv[1],"--ctr-test-child"))return interrupted_report(argv[2],argv[3]);
    checks+=test_audio_stream_app();
    storage_tests();
    return_directory_tests();
    slice_tests();guide_tests();
    lifecycle_tests();
    exit_order_tests();
    scale_tests();
    scale_cache_tests();
    char directory[384],text[4096];
    CHECK(ctr_test_temp_directory(directory,sizeof(directory),"ctr2wup-report"));
    result_tests(directory);
    commit_tests(directory);
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
#ifdef _WIN32
    /* A native pipe with its reader closed produces a real write/flush error.
       Windows has no /dev/full and does not raise POSIX SIGPIPE. */
    int failed_pipe[2];CHECK(ctr_test_pipe(failed_pipe)==0);
    full.file=ctr_test_fdopen(failed_pipe[1],"wb");
    CHECK(ctr_test_close(failed_pipe[0])==0);
#else
    full.file = fopen("/dev/full", "w");
#endif
    CHECK(full.file != NULL);
    CHECK(!ctr_test_report_line(&full, "A write failure must stay visible"));
    CHECK(full.failed && full.error_code != 0);
    CHECK(!ctr_test_report_line(&full, "Never clear the failed status"));
    CHECK(!ctr_test_report_close(&full));
    CHECK(full.file == NULL);
    CHECK(!ctr_test_report_checkpoint(&full,"Must not hide the prior SD error"));
    CHECK(full.file == NULL);
    read_file(first.path,text,sizeof(text));
    CHECK(strstr(text,"CPU_selftest PASS result=0")!=NULL &&
          strstr(text,"A write failure must stay visible")==NULL);
    CHECK(ctr_test_unlink(second.path)==0);
    CHECK(!ctr_test_report_checkpoint(&second,"Never recreate a missing result"));
    CHECK(second.failed && second.error_code==ENOENT);
    CHECK(ctr_test_access(second.path,F_OK)!=0);
    ctr_test_report busy={0};
    CHECK(ctr_test_report_open(&busy,directory,"native","0.14.2",2));
    CHECK(!ctr_test_report_checkpoint(&busy,"Reject an active stream"));
    CHECK(busy.failed && busy.error_code==EBUSY);
    CHECK(!ctr_test_report_close(&busy));
    CHECK(busy.file==NULL);
    CHECK(ctr_test_unlink(busy.path)==0);
    CHECK(ctr_test_unlink(first.path) == 0); CHECK(ctr_test_remove_directory(directory) == 0);
    printf("{\"ok\":true,\"assertions\":%u,\"text_reports\":true,\"console_verified\":false}\n", checks);
    return 0;
}
