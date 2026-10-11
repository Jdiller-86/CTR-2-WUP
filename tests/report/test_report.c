#define _POSIX_C_SOURCE 200809L
#include "test_report.h"
#include "test_guide.h"
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
    in.held=CTR_GUIDE_A;ctr_test_guide_update(&g,&in);CHECK(g.step==2);in.held=0;
    touches(&g,&in);CHECK(g.step==3);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.step==4);
    in.pressed=0;in.guest_sessions=1;in.guest_exited=true;in.guest_pass=true;
    ctr_test_guide_update(&g,&in);CHECK(g.step==5);
    in.pressed=CTR_GUIDE_PLUS;ctr_test_guide_update(&g,&in);CHECK(ctr_test_guide_take_restart(&g));
    in.pressed=0;ctr_test_guide_update(&g,&in);CHECK(g.step==5);
    in.guest_sessions=2;ctr_test_guide_update(&g,&in);CHECK(g.step==6 && ctr_test_guide_take_restart(&g));
    in.guest_exited=false;in.guest_pass=false;in.milliseconds=600000;ctr_test_guide_update(&g,&in);CHECK(g.step==7);
    in.pressed=CTR_GUIDE_R;ctr_test_guide_update(&g,&in);CHECK(g.done && g.passed==8);
    ctr_test_guide_init(&g,CTR_GUIDE_INTERACTIVE,capture,NULL);in.pressed=0;in.guest_fault=true;
    ctr_test_guide_update(&g,&in);CHECK(g.failed==1 && !g.passed);
}

static void read_file(const char *path, char *buffer, size_t size) {
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    size_t n = fread(buffer, 1, size - 1, f);
    buffer[n] = 0;
    CHECK(!ferror(f));
    CHECK(fclose(f) == 0);
}

int main(void) {
    guide_tests();
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
    CHECK(unlink(first.path) == 0); CHECK(unlink(second.path) == 0); CHECK(rmdir(directory) == 0);
    printf("{\"ok\":true,\"assertions\":%u,\"text_reports\":true,\"console_verified\":false}\n", checks);
    return 0;
}
