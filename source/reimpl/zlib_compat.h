#ifndef REIMPL_ZLIB_COMPAT_H
#define REIMPL_ZLIB_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef void *voidpf;

typedef voidpf (*alloc_func)(voidpf opaque, unsigned items, unsigned size);
typedef void (*free_func)(voidpf opaque, voidpf address);

struct internal_state;

typedef struct z_stream_s {
    Bytef *next_in;
    uInt avail_in;
    uLong total_in;

    Bytef *next_out;
    uInt avail_out;
    uLong total_out;

    char *msg;
    struct internal_state *state;

    alloc_func zalloc;
    free_func zfree;
    voidpf opaque;

    int data_type;
    uLong adler;
    uLong reserved;
} z_stream;

typedef z_stream *z_streamp;

const char *zlibVersion(void);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);
int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

#define Z_NO_FLUSH 0
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NULL 0
#define MAX_WBITS 15

#endif
