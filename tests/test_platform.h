/* Test-only native temporary directories and abrupt child processes. */
#ifndef CTR_TEST_PLATFORM_H
#define CTR_TEST_PLATFORM_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#ifndef F_OK
#define F_OK 0
#endif
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* A crash must bypass CRT exit handlers and DLL detach callbacks on Windows. */
static inline _Noreturn void ctr_test_crash_exit(unsigned status) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(),status);
    _Exit(255); /* Unexpected termination failure must not masquerade as the requested crash. */
#else
    _Exit((int)status);
#endif
}

typedef int (*ctr_test_child_body)(const char *mode,const char *directory);
typedef struct {
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
} ctr_test_child;

static inline bool ctr_test_temp_directory(char *path,size_t capacity,const char *prefix) {
#ifdef _WIN32
    char root[32768];static unsigned sequence;
    DWORD length=GetTempPathA((DWORD)sizeof(root),root);
    if(!length || length>=sizeof(root))return false;
    for(DWORD i=0;i<length;++i)if(root[i]=='\\')root[i]='/';
    for(unsigned attempt=0;attempt<64;++attempt) {
        int n=snprintf(path,capacity,"%s%s-%lu-%llu-%u",root,prefix,
            (unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64(),sequence++);
        if(n<0 || (size_t)n>=capacity) { errno=ENAMETOOLONG;return false; }
        if(CreateDirectoryA(path,NULL))return true;
        if(GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    }
    errno=EEXIST;return false;
#else
    const char *root=getenv("TMPDIR");if(!root || !*root)root="/tmp";
    int n=snprintf(path,capacity,"%s/%s-XXXXXX",root,prefix);
    if(n<0 || (size_t)n>=capacity) { errno=ENAMETOOLONG;return false; }
    return mkdtemp(path)!=NULL;
#endif
}

static inline int ctr_test_access(const char *path,int mode) {
#ifdef _WIN32
    return _access(path,mode);
#else
    return access(path,mode);
#endif
}
static inline int ctr_test_unlink(const char *path) {
#ifdef _WIN32
    return _unlink(path);
#else
    return unlink(path);
#endif
}
static inline int ctr_test_remove_directory(const char *path) {
#ifdef _WIN32
    return _rmdir(path);
#else
    return rmdir(path);
#endif
}
static inline int ctr_test_pipe(int descriptors[2]) {
#ifdef _WIN32
    return _pipe(descriptors,4096,_O_BINARY);
#else
    return pipe(descriptors);
#endif
}
static inline FILE *ctr_test_fdopen(int descriptor,const char *mode) {
#ifdef _WIN32
    return _fdopen(descriptor,mode);
#else
    return fdopen(descriptor,mode);
#endif
}
static inline int ctr_test_close(int descriptor) {
#ifdef _WIN32
    return _close(descriptor);
#else
    return close(descriptor);
#endif
}

static inline bool ctr_test_child_start(ctr_test_child *child,const char *mode,
                                       const char *directory,ctr_test_child_body body) {
#ifdef _WIN32
    char executable[32768],command[32768];
    DWORD n=GetModuleFileNameA(NULL,executable,(DWORD)sizeof(executable));
    if(!n || n>=sizeof(executable))return false;
    /* Modes and exclusive directory names are test-controlled. Windows paths
       cannot contain double quotes; quote both paths to preserve spaces. */
    if(strchr(mode,'"') || strchr(mode,' ') || strchr(directory,'"'))return false;
    int count=snprintf(command,sizeof(command),"\"%s\" --ctr-test-child %s \"%s\"",executable,mode,directory);
    if(count<0 || (size_t)count>=sizeof(command))return false;
    STARTUPINFOA startup={0};PROCESS_INFORMATION process={0};startup.cb=sizeof(startup);
    (void)body;
    if(!CreateProcessA(executable,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process))return false;
    CloseHandle(process.hThread);child->process=process.hProcess;return true;
#else
    child->pid=fork();
    if(!child->pid)_Exit(body(mode,directory));
    return child->pid>0;
#endif
}
static inline bool ctr_test_child_wait(ctr_test_child *child,unsigned *exit_status) {
#ifdef _WIN32
    DWORD code=0;DWORD waited=WaitForSingleObject(child->process,60000);
    bool ok=waited==WAIT_OBJECT_0 && GetExitCodeProcess(child->process,&code);
    if(waited!=WAIT_OBJECT_0)TerminateProcess(child->process,255);
    CloseHandle(child->process);child->process=NULL;
    if(ok)*exit_status=(unsigned)code;
    return ok;
#else
    int status;pid_t result;
    do { result=waitpid(child->pid,&status,0); } while(result<0 && errno==EINTR);
    if(result!=child->pid || !WIFEXITED(status))return false;
    *exit_status=(unsigned)WEXITSTATUS(status);return true;
#endif
}
#endif
