/**
 * Legacy CRT entry points for the prebuilt import libraries shipped in compat/.
 *
 * libcurl.x64.lib and pthreadVC2.lib were produced against msvcrt.dll, which
 * exported __iob_func, longjmp and strncat_s. The UCRT that ships with modern
 * MSVC provides those statically instead, so the DLL-import symbols the old
 * libraries reference no longer exist. Redirecting them here keeps the vendored
 * binaries usable without rebuilding them.
 *
 * Only needed for the MSVC build; MinGW links its own CRT.
 *
 * Copyright 2026-2027 d0wn3d
 */

#ifdef _MSC_VER

#include <stdio.h>
#include <setjmp.h>
#include <string.h>

/* msvcrt.dll exposed the three standard streams as one array */
FILE *__cdecl __iob_func(void)
{
	static FILE *streams[3];
	streams[0] = stdin;
	streams[1] = stdout;
	streams[2] = stderr;
	return (FILE *)streams;
}

/* __imp_* is the indirection slot an import would have filled in; pointing it
 * at the static UCRT implementation makes the old call sites resolve. */
void (__cdecl *__imp_longjmp)(jmp_buf, int) = longjmp;
errno_t (__cdecl *__imp_strncat_s)(char *, rsize_t, const char *, rsize_t) = strncat_s;

#endif /* _MSC_VER */
