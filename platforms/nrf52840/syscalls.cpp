/*
 * SECD Machine for Microcontrollers - SAMD21 newlib syscall stubs
 * Copyright (C) 2026  License: GPL3
 *
 * Minimal newlib backend: _sbrk over the linker-script .heap region plus
 * the standard stubs so the C library (calloc/free for the VM object heap)
 * links and runs on bare metal.
 */

#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern char __heap_start;
extern char __heap_end;

extern "C" void *_sbrk(ptrdiff_t incr) {
    static char *cur = NULL;
    if (cur == NULL) {
        cur = &__heap_start;
    }
    char *prev = cur;
    if ((cur + incr > &__heap_end) || (cur + incr < prev)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    cur += incr;
    return prev;
}

extern "C" int _write(int fd, const void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

extern "C" int _read(int fd, void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

extern "C" int _lseek(int fd, int offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

extern "C" int _close(int fd) {
    (void)fd;
    return -1;
}

extern "C" int _fstat(int fd, void *st) {
    (void)fd;
    (void)st;
    return -1;
}

extern "C" int _isatty(int fd) {
    (void)fd;
    return 0;
}

extern "C" int _getpid(void) {
    return 1;
}

extern "C" int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    return -1;
}

extern "C" void _exit(int status) {
    (void)status;
    for (;;) {
    }
}
