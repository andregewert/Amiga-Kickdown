/*
 * mdtree_dos - the file system of mdtree with dos.library
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "fileio.h"
#include "mdtree.h"

#define COPY_BUFFER 16384

struct dir {
    BPTR lock;
    struct FileInfoBlock *fib;  /* must be longword aligned, so not on the stack */
};

void *mdfs_open_dir(const char *path)
{
    struct dir *d;
    LONG err;

    if (!(d = AllocVec(sizeof(*d), MEMF_ANY | MEMF_CLEAR))) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    if ((d->fib = AllocDosObject(DOS_FIB, NULL)) && (d->lock = Lock((STRPTR)path, ACCESS_READ)) &&
        Examine(d->lock, d->fib))
        return d;
    err = d->fib ? IoErr() : ERROR_NO_FREE_STORE;
    mdfs_close_dir(d);
    SetIoErr(err);
    return NULL;
}

int mdfs_next(void *dir, char *name, int *is_dir)
{
    struct dir *d = dir;

    for (;;) {
        LONG type;

        if (!ExNext(d->lock, d->fib)) return IoErr() == ERROR_NO_MORE_ENTRIES ? 0 : -1;
        type = d->fib->fib_DirEntryType;
        if (type == ST_SOFTLINK || type == ST_LINKDIR) continue;    /* no loops */
        strncpy(name, d->fib->fib_FileName, MDFS_NAMEMAX - 1);
        name[MDFS_NAMEMAX - 1] = 0;
        *is_dir = type > 0;
        return 1;
    }
}

void mdfs_close_dir(void *dir)
{
    struct dir *d = dir;

    if (d->lock) UnLock(d->lock);
    if (d->fib) FreeDosObject(DOS_FIB, d->fib);
    FreeVec(d);
}

int mdfs_exists(const char *path)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    int result = MDFS_NONE;

    if (!(lock = Lock((STRPTR)path, ACCESS_READ))) return MDFS_NONE;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        /* a lock is enough to say it exists, should Examine() fail */
        result = MDFS_FILE;
        if (Examine(lock, fib) && fib->fib_DirEntryType > 0) result = MDFS_DIR;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return result;
}

int mdfs_make_dir(const char *path)
{
    BPTR lock = CreateDir((STRPTR)path);
    LONG err;

    if (lock) {
        UnLock(lock);
        return 1;
    }
    err = IoErr();
    if (mdfs_exists(path) == MDFS_DIR) return 1;
    SetIoErr(err);
    return 0;
}

char *mdfs_read(const char *path, size_t *len)
{
    ULONG size = 0;
    STRPTR data = read_file((CONST_STRPTR)path, &size);

    if (data && len) *len = size;
    return (char *)data;
}

void mdfs_free(char *data)
{
    if (data) FreeVec(data);
}

int mdfs_write(const char *path, const char *data, size_t len)
{
    return write_file((CONST_STRPTR)path, (CONST_STRPTR)data, len);
}

int mdfs_copy(const char *from, const char *to)
{
    BPTR in = 0, out = 0;
    APTR buffer;
    LONG n = 0, err = 0;
    int ok = 0;

    if (!(buffer = AllocVec(COPY_BUFFER, MEMF_ANY))) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return 0;
    }
    if ((in = Open((STRPTR)from, MODE_OLDFILE)) && (out = Open((STRPTR)to, MODE_NEWFILE))) {
        while ((n = Read(in, buffer, COPY_BUFFER)) > 0)
            if (Write(out, buffer, n) != n) break;
        ok = n == 0;
    }
    if (!ok) err = IoErr();
    if (out && !Close(out)) {
        if (ok) err = IoErr();
        ok = 0;
    }
    if (in) Close(in);
    FreeVec(buffer);
    if (!ok) SetIoErr(err);
    return ok;
}

int mdfs_is_inside(const char *inner, const char *outer)
{
    BPTR lo, li;
    int inside = 0;

    if (!(lo = Lock((STRPTR)outer, ACCESS_READ))) return 0;
    if ((li = Lock((STRPTR)inner, ACCESS_READ))) {
        while (li) {
            BPTR parent;
            if (SameLock(li, lo) == LOCK_SAME) {
                inside = 1;
                break;
            }
            parent = ParentDir(li);
            UnLock(li);
            li = parent;
        }
        if (li) UnLock(li);
    }
    UnLock(lo);
    return inside;
}

void mdfs_print(const char *text)
{
    PutStr((STRPTR)text);
}

void mdfs_fault(const char *path)
{
    PrintFault(IoErr(), (STRPTR)path);
}

int mdfs_break(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}
