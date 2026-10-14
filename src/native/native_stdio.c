// native_stdio.c - the streams of src/web/compat/bo1_stdio.h: a FILE of this program (low memory) around the host's.
//
// Compiled without the engine's force-included headers and not rewritten: FILE below is the C library's.
#if defined(BO1_NATIVE) && defined(__LP64__)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bo1_file
{
    FILE *host;
} bo1_file;

// src/web/compat/web_path.cpp: '\' or '/', relative to the tracked directory, case-insensitive; 1 if it exists
int bo1_web_resolve_path(const char *in, char *out, int outSize);
void bo1_web_invalidate_path(const char *resolvedPath);

static bo1_file *Wrap(FILE *host)
{
    if (!host)
        return NULL;
    bo1_file *file = (bo1_file *)malloc(sizeof(bo1_file));   // this program's malloc: below 4 GB
    if (!file)
    {
        fclose(host);
        return NULL;
    }
    file->host = host;
    return file;
}

bo1_file *bo1_stdstream(int which)
{
    static bo1_file streams[3];
    bo1_file *file = &streams[which < 0 || which > 2 ? 2 : which];
    if (!file->host)
        file->host = which == 0 ? stdin : which == 1 ? stdout : stderr;
    return file;
}

bo1_file *bo1_fopen(const char *path, const char *mode)
{
    if (!path)
        return NULL;
    char real[1024];
    const int exists = bo1_web_resolve_path(path, real, (int)sizeof(real));
    FILE *host = fopen(real, mode);
    if (host && !exists)
        bo1_web_invalidate_path(real);   // a file was created: the directory listing cache is stale
    return Wrap(host);
}

bo1_file *bo1_freopen(const char *path, const char *mode, bo1_file *file)
{
    if (!file)
        return NULL;
    char real[1024];
    const char *target = path;
    if (path)
    {
        bo1_web_resolve_path(path, real, (int)sizeof(real));
        target = real;
    }
    FILE *host = freopen(target, mode, file->host);
    if (!host)
        return NULL;
    file->host = host;
    return file;
}

bo1_file *bo1_fdopen(int fd, const char *mode)
{
    return Wrap(fdopen(fd, mode));
}

bo1_file *bo1_tmpfile(void)
{
    return Wrap(tmpfile());
}

int bo1_fclose(bo1_file *file)
{
    if (!file)
        return EOF;
    const int result = fclose(file->host);
    if (file != bo1_stdstream(0) && file != bo1_stdstream(1) && file != bo1_stdstream(2))
        free(file);
    return result;
}

size_t bo1_fread(void *buffer, size_t size, size_t count, bo1_file *file)
{
    return fread(buffer, size, count, file->host);
}

size_t bo1_fwrite(const void *buffer, size_t size, size_t count, bo1_file *file)
{
    return fwrite(buffer, size, count, file->host);
}

// the engine's long is 32 bits (bo1_stdio.h)
int bo1_fseek(bo1_file *file, int offset, int origin)
{
    return fseek(file->host, (long)offset, origin);
}

int bo1_ftell(bo1_file *file)
{
    return (int)ftell(file->host);
}

int bo1_fseeko(bo1_file *file, long long offset, int origin)
{
    return fseeko(file->host, (off_t)offset, origin);
}

long long bo1_ftello(bo1_file *file)
{
    return (long long)ftello(file->host);
}

void bo1_rewind(bo1_file *file)
{
    rewind(file->host);
}

int bo1_fflush(bo1_file *file)
{
    return fflush(file ? file->host : NULL);   // fflush(NULL): every stream
}

int bo1_fgetc(bo1_file *file)
{
    return fgetc(file->host);
}

int bo1_ungetc(int c, bo1_file *file)
{
    return ungetc(c, file->host);
}

int bo1_fputc(int c, bo1_file *file)
{
    return fputc(c, file->host);
}

char *bo1_fgets(char *buffer, int size, bo1_file *file)
{
    return fgets(buffer, size, file->host);
}

int bo1_fputs(const char *text, bo1_file *file)
{
    return fputs(text, file->host);
}

int bo1_vfprintf(bo1_file *file, const char *format, va_list args)
{
    return vfprintf(file->host, format, args);
}

int bo1_fprintf(bo1_file *file, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vfprintf(file->host, format, args);
    va_end(args);
    return result;
}

int bo1_vfscanf(bo1_file *file, const char *format, va_list args)
{
    return vfscanf(file->host, format, args);
}

int bo1_fscanf(bo1_file *file, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = vfscanf(file->host, format, args);
    va_end(args);
    return result;
}

int bo1_feof(bo1_file *file)
{
    return feof(file->host);
}

int bo1_ferror(bo1_file *file)
{
    return ferror(file->host);
}

void bo1_clearerr(bo1_file *file)
{
    clearerr(file->host);
}

int bo1_fileno(bo1_file *file)
{
    return fileno(file->host);
}

int bo1_setvbuf(bo1_file *file, char *buffer, int mode, size_t size)
{
    return setvbuf(file->host, buffer, mode, size);
}

void bo1_setbuf(bo1_file *file, char *buffer)
{
    setbuf(file->host, buffer);
}

#endif
