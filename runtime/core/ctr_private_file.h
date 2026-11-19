#ifndef CTR_PRIVATE_FILE_H
#define CTR_PRIVATE_FILE_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

/* Create at a platform-selected path. O_EXCL preserves existing files; POSIX
 * hosts grant access only to the owner even under a permissive umask. */
static inline FILE *ctr_create_private_file(const char *path, bool binary) {
    const char *mode = binary ? "wb" : "w";
#ifdef _WIN32
    int descriptor = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | (binary ? _O_BINARY : _O_TEXT),
                           _S_IREAD | _S_IWRITE);
    if (descriptor < 0) return NULL;
    FILE *file = _fdopen(descriptor, mode);
#else
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) return NULL;
    FILE *file = fdopen(descriptor, mode);
#endif
    if (file) return file;

    int error = errno;
#ifdef _WIN32
    _close(descriptor);
#else
    close(descriptor);
#endif
    remove(path);
    errno = error;
    return NULL;
}

#endif
