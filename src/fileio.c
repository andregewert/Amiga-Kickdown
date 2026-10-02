/*
 * fileio - whole-file reading and writing via dos.library
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "fileio.h"

STRPTR read_fh(BPTR fh, ULONG *len)
{
    ULONG size = 0, alloc = 8192;
    STRPTR data = AllocVec(alloc, MEMF_ANY), bigger;
    LONG n;

    if (!data) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    for (;;) {
        if (size + 1 >= alloc) {
            if (!(bigger = AllocVec(alloc * 2, MEMF_ANY))) {
                FreeVec(data);
                SetIoErr(ERROR_NO_FREE_STORE);
                return NULL;
            }
            CopyMem(data, bigger, size);
            FreeVec(data);
            data = bigger;
            alloc *= 2;
        }
        n = Read(fh, data + size, alloc - size - 1);
        if (n < 0) {
            FreeVec(data);
            return NULL;
        }
        if (n == 0) break;
        size += n;
    }
    data[size] = 0;
    if (len) *len = size;
    return data;
}

STRPTR read_file(CONST_STRPTR name, ULONG *len)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    STRPTR data;
    LONG size, err;

    if (!fh) return NULL;
    /* Seek() returns the previous position: the size after the second call */
    if (Seek(fh, 0, OFFSET_END) < 0 || (size = Seek(fh, 0, OFFSET_BEGINNING)) < 0) {
        /* not seekable (e.g. PIPE:), read until EOF */
        data = read_fh(fh, len);
        err = IoErr();
        Close(fh);
        SetIoErr(err);
        return data;
    }
    if (!(data = AllocVec(size + 1, MEMF_ANY))) {
        Close(fh);
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    if (Read(fh, data, size) != size) {
        err = IoErr();
        FreeVec(data);
        Close(fh);
        SetIoErr(err ? err : ERROR_SEEK_ERROR);
        return NULL;
    }
    Close(fh);
    data[size] = 0;
    if (len) *len = size;
    return data;
}

BOOL write_fh(BPTR fh, CONST_STRPTR data, ULONG len)
{
    return Write(fh, (APTR)data, len) == (LONG)len;
}

BOOL write_file(CONST_STRPTR name, CONST_STRPTR data, ULONG len)
{
    BPTR fh = Open((STRPTR)name, MODE_NEWFILE);
    BOOL ok;
    LONG err;

    if (!fh) return FALSE;
    ok = write_fh(fh, data, len);
    err = IoErr();
    if (!Close(fh)) ok = FALSE;
    else if (!ok) SetIoErr(err);
    return ok;
}
