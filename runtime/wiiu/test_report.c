#include "test_report.h"

#include <errno.h>
#include <string.h>

static bool fail(ctr_test_report *r, int error) {
    r->failed = true;
    r->error_code = error ? error : EIO;
    return false;
}

bool ctr_test_report_line(ctr_test_report *r, const char *message) {
    if (!r || !message) return false;
    if (!r->file || r->failed) return fail(r, r->error_code);
    if (fprintf(r->file, "%s\n", message) < 0 || fflush(r->file) != 0)
        return fail(r, errno);
    return true;
}

bool ctr_test_report_open(ctr_test_report *r, const char *directory,
                          const char *application, const char *version, uint64_t id) {
    if (!r) return false;
    if (r->file) return fail(r, EBUSY);
    memset(r, 0, sizeof(*r));
    if (!directory || !*directory || !application || !*application || !version ||
        strlen(application) > 48 || strlen(version) > 32 ||
        strspn(application, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(application) ||
        strspn(version, "0123456789.") != strlen(version) || !*version)
        return fail(r, EINVAL);
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        int n = snprintf(r->filename, sizeof(r->filename), "%s-%016llx-%02u.txt",
                         application, (unsigned long long)id, attempt);
        if (n < 0 || (size_t)n >= sizeof(r->filename)) return fail(r, ENAMETOOLONG);
        n = snprintf(r->path, sizeof(r->path), "%s/%s", directory, r->filename);
        if (n < 0 || (size_t)n >= sizeof(r->path)) return fail(r, ENAMETOOLONG);
        errno = 0;
        r->file = fopen(r->path, "wx");
        if (r->file) break;
        if (errno != EEXIST) return fail(r, errno);
    }
    if (!r->file) return fail(r, EEXIST);
    char line[160];
    if (!ctr_test_report_line(r, "CTR2WUP Wii U test report")) return false;
    snprintf(line, sizeof(line), "Application: %s | Build: %s", application, version);
    if (!ctr_test_report_line(r, line) ||
        !ctr_test_report_line(r, "Upload this .txt file from sd:/ctr2wup/ after the test.") ||
        !ctr_test_report_line(r, "Session STARTED. Missing END OF REPORT marker means interrupted/incomplete evidence.") ||
        !ctr_test_report_line(r, "PASS messages cover the named software checks, not commercial compatibility.") ||
        !ctr_test_report_line(r, "Picture alignment/colors, audible sound and launcher return need your brief observation.") ||
        !ctr_test_report_line(r, "Session IDs are console ticks; flushes do not guarantee power-loss durability.") ||
        !ctr_test_report_line(r, "END OF REPORT records resource cleanup only; EXIT checkpoints and your launcher-return observation are separate.") ||
        !ctr_test_report_line(r, "===== DIAGNOSTIC LOG =====")) return false;
    return true;
}

bool ctr_test_report_close(ctr_test_report *r) {
    if (!r) return false;
    if (r->file) {
        FILE *file = r->file;
        r->file = NULL;
        if (fclose(file) != 0) return fail(r, errno);
    }
    return !r->failed;
}

bool ctr_test_report_checkpoint(ctr_test_report *r, const char *message) {
    if(!r || !message)return false;
    if(r->failed)return false;
    if(r->file)return fail(r,EBUSY);
    if(!r->path[0])return fail(r,EINVAL);
    r->file=fopen(r->path,"r+");
    if(!r->file)return fail(r,errno);
    if(fseek(r->file,0,SEEK_END)!=0)fail(r,errno);
    bool written=!r->failed && ctr_test_report_line(r,message);
    bool closed=ctr_test_report_close(r);
    return written && closed;
}
