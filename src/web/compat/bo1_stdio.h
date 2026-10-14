// bo1_stdio.h - <stdio.h> streams as objects of this program, for the 32-bit-pointer build (tools/p32).
//
// The engine keeps FILE pointers in its structures (fsh[].handleFiles.file.o, r_logFileGlob.fp, ...), and in that
// build a pointer object is 4 bytes. The C library's FILE objects are the C library's: on Android they come from its
// own allocator, far above 4 GB, and nothing an application defines changes that. So FILE becomes a small struct
// allocated by this program (below 4 GB) that holds the host's FILE *, and the stdio functions the engine uses take
// it (src/native/native_stdio.c). Included from bo1_web_prelude.h after <stdio.h>.
//
// fopen also resolves the path as the engine's Win32-style paths need ('\', letter case), which the linker's
// --wrap=fopen does for the other builds.
#pragma once
#if defined(BO1_P32)
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bo1_file bo1_file;

bo1_file *bo1_fopen(const char *path, const char *mode);
bo1_file *bo1_freopen(const char *path, const char *mode, bo1_file *file);
bo1_file *bo1_fdopen(int fd, const char *mode);
bo1_file *bo1_tmpfile(void);
int bo1_fclose(bo1_file *file);
size_t bo1_fread(void *buffer, size_t size, size_t count, bo1_file *file);
size_t bo1_fwrite(const void *buffer, size_t size, size_t count, bo1_file *file);
// int, not long: this header is rewritten (long -> int) and native_stdio.c is not, and the two must agree on the
// width (AArch64 leaves the upper half of a 32-bit argument's register undefined)
int bo1_fseek(bo1_file *file, int offset, int origin);
int bo1_ftell(bo1_file *file);
int bo1_fseeko(bo1_file *file, long long offset, int origin);
long long bo1_ftello(bo1_file *file);
void bo1_rewind(bo1_file *file);
int bo1_fflush(bo1_file *file);
int bo1_fgetc(bo1_file *file);
int bo1_ungetc(int c, bo1_file *file);
int bo1_fputc(int c, bo1_file *file);
char *bo1_fgets(char *buffer, int size, bo1_file *file);
int bo1_fputs(const char *text, bo1_file *file);
int bo1_fprintf(bo1_file *file, const char *format, ...) __attribute__((format(printf, 2, 3)));
int bo1_vfprintf(bo1_file *file, const char *format, va_list args);
int bo1_fscanf(bo1_file *file, const char *format, ...) __attribute__((format(scanf, 2, 3)));
int bo1_vfscanf(bo1_file *file, const char *format, va_list args);
int bo1_feof(bo1_file *file);
int bo1_ferror(bo1_file *file);
void bo1_clearerr(bo1_file *file);
int bo1_fileno(bo1_file *file);
int bo1_setvbuf(bo1_file *file, char *buffer, int mode, size_t size);
void bo1_setbuf(bo1_file *file, char *buffer);
bo1_file *bo1_stdstream(int which);   // 0 stdin, 1 stdout, 2 stderr

#ifdef __cplusplus
}
#endif

#undef stdin
#undef stdout
#undef stderr
#undef getc
#undef putc
#undef feof
#undef ferror
#undef clearerr
#undef fileno
#define FILE bo1_file
#define stdin bo1_stdstream(0)
#define stdout bo1_stdstream(1)
#define stderr bo1_stdstream(2)
#define fopen bo1_fopen
#define fopen64 bo1_fopen
#define freopen bo1_freopen
#define fdopen bo1_fdopen
#define tmpfile bo1_tmpfile
#define fclose bo1_fclose
#define fread bo1_fread
#define fwrite bo1_fwrite
#define fseek bo1_fseek
#define ftell bo1_ftell
#define fseeko bo1_fseeko
#define ftello bo1_ftello
#define rewind bo1_rewind
#define fflush bo1_fflush
#define fgetc bo1_fgetc
#define getc bo1_fgetc
#define ungetc bo1_ungetc
#define fputc bo1_fputc
#define putc bo1_fputc
#define fgets bo1_fgets
#define fputs bo1_fputs
#define fprintf bo1_fprintf
#define vfprintf bo1_vfprintf
#define fscanf bo1_fscanf
#define vfscanf bo1_vfscanf
#define feof bo1_feof
#define ferror bo1_ferror
#define clearerr bo1_clearerr
#define fileno bo1_fileno
#define setvbuf bo1_setvbuf
#define setbuf bo1_setbuf
#endif
