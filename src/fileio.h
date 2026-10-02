/*
 * fileio - whole-file reading and writing via dos.library
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef FILEIO_H
#define FILEIO_H

#include <exec/types.h>
#include <dos/dos.h>

/* Reads a whole file (or, for fh, everything up to EOF) into a NUL
 * terminated buffer (AllocVec(), release with FreeVec()). Returns NULL
 * on error, IoErr() tells why. *len (optional) gets the size.           */
STRPTR read_file(CONST_STRPTR name, ULONG *len);
STRPTR read_fh(BPTR fh, ULONG *len);

/* Writes len bytes to a new file (or to fh). FALSE on error, see IoErr(). */
BOOL write_file(CONST_STRPTR name, CONST_STRPTR data, ULONG len);
BOOL write_fh(BPTR fh, CONST_STRPTR data, ULONG len);

#endif /* FILEIO_H */
