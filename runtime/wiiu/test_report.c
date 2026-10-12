#define _POSIX_C_SOURCE 200809L
#include "test_report.h"

#include <errno.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static bool fail(ctr_test_report *r, int error, const char *stage) {
    if (!r->failed) {
        r->failed = true;
        r->error_code = error ? error : EIO;
        r->error_stage = stage;
    }
    return false;
}

bool ctr_test_report_line(ctr_test_report *r, const char *message) {
    if (!r || !message) return false;
    if (!r->file || r->failed) return fail(r, EBADF, "write");
    if (fprintf(r->file, "%s\n", message) < 0 || fflush(r->file) != 0)
        return fail(r, errno, "write");
    return true;
}

bool ctr_test_report_open(ctr_test_report *r, const char *directory,
                          const char *application, const char *version, uint64_t id) {
    if (!r) return false;
    if (r->file) return fail(r, EBUSY, "open");
    memset(r, 0, sizeof(*r));
    if (!directory || !*directory || !application || !*application || !version ||
        strlen(application) > 48 || strlen(version) > 32 ||
        strspn(application, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(application) ||
        strspn(version, "0123456789.") != strlen(version) || !*version)
        return fail(r, EINVAL, "arguments");
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        int n = snprintf(r->filename, sizeof(r->filename), "%s-%016llx-%02u.txt",
                         application, (unsigned long long)id, attempt);
        if (n < 0 || (size_t)n >= sizeof(r->filename)) return fail(r, ENAMETOOLONG, "filename");
        n = snprintf(r->path, sizeof(r->path), "%s/%s", directory, r->filename);
        if (n < 0 || (size_t)n >= sizeof(r->path)) return fail(r, ENAMETOOLONG, "path");
        errno = 0;
        r->file = fopen(r->path, "wx");
        if (r->file) break;
        if (errno != EEXIST) return fail(r, errno, "open");
    }
    if (!r->file) return fail(r, EEXIST, "open");
    char line[160];
    if (!ctr_test_report_line(r, "CTR2WUP Wii U test report")) return false;
    snprintf(line, sizeof(line), "Application: %s | Build: %s", application, version);
    if (!ctr_test_report_line(r, line) ||
        !ctr_test_report_line(r, "Upload this .txt file from sd:/ctr2wup/ after the test.") ||
        !ctr_test_report_line(r, "Session STARTED. Missing END OF REPORT marker means interrupted/incomplete evidence.") ||
        !ctr_test_report_line(r, "PASS messages cover the named software checks, not commercial compatibility.") ||
        !ctr_test_report_line(r, "Picture alignment/colors, audible sound and launcher return need your brief observation.") ||
        !ctr_test_report_line(r, "Session IDs are console ticks; flushes do not guarantee power-loss durability.") ||
        !ctr_test_report_line(r, "Active-test checkpoints sync/close then request mounted-volume sync; exit checkpoints sync/close; crash recovery remains unverified.") ||
        !ctr_test_report_line(r, "END OF REPORT records resource cleanup only; EXIT checkpoints and your launcher-return observation are separate.") ||
        !ctr_test_report_line(r, "===== DIAGNOSTIC LOG =====")) return false;
    return true;
}

void ctr_test_report_set_volume_sync(ctr_test_report *r, ctr_report_volume_sync sync,
                                    void *context) {
    if (!r) return;
    r->volume_sync = sync;
    r->sync_context = context;
}

bool ctr_test_report_close(ctr_test_report *r) {
    if (!r) return false;
    if (r->file) {
        FILE *file = r->file;
        if (!r->failed && fflush(file) != 0) fail(r, errno, "flush");
        if (!r->failed) {
            int status;
#ifdef _WIN32
            do { status = _commit(_fileno(file)); } while (status != 0 && errno == EINTR);
#else
            do { status = fsync(fileno(file)); } while (status != 0 && errno == EINTR);
#endif
            if (status != 0) fail(r, errno, "file_sync");
        }
        r->file = NULL;
        if (fclose(file) != 0) fail(r, errno, "close");
        if (!r->failed && r->volume_sync && !r->volume_sync(r->sync_context))
            fail(r, errno, "volume_sync");
        if (!r->failed) ++r->commits;
    }
    return !r->failed;
}

bool ctr_test_report_commit(ctr_test_report *r) {
    if (!r) return false;
    if (r->failed) return false;
    if (!r->file) return fail(r, EBADF, "commit");
    if (!ctr_test_report_close(r)) return false;
    r->file = fopen(r->path, "r+");
    if (!r->file) return fail(r, errno, "reopen");
    if (fseek(r->file, 0, SEEK_END) != 0) {
        fail(r, errno, "seek");
        ctr_test_report_close(r);
        return false;
    }
    return true;
}

bool ctr_test_report_checkpoint(ctr_test_report *r, const char *message) {
    if(!r || !message)return false;
    if(r->failed)return false;
    if(r->file)return fail(r,EBUSY,"checkpoint");
    if(!r->path[0])return fail(r,EINVAL,"checkpoint");
    r->file=fopen(r->path,"r+");
    if(!r->file)return fail(r,errno,"reopen");
    if(fseek(r->file,0,SEEK_END)!=0)fail(r,errno,"seek");
    bool written=!r->failed && ctr_test_report_line(r,message);
    bool closed=ctr_test_report_close(r);
    return written && closed;
}
