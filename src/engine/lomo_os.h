#ifndef LOMO_OS_H
#define LOMO_OS_H

#include <stddef.h>
#include <stdint.h>

// Memory Management
void* lomo_aligned_malloc(size_t size, size_t alignment);
void* lomo_aligned_realloc(void* ptr, size_t size, size_t alignment);
void lomo_aligned_free(void* ptr);

// Synchronization
typedef struct LomoMutex {
    void* handle;
} LomoMutex;

void lomo_mutex_init(LomoMutex* mutex);
void lomo_mutex_destroy(LomoMutex* mutex);
void lomo_mutex_lock(LomoMutex* mutex);
void lomo_mutex_unlock(LomoMutex* mutex);

// Memory Mapping
typedef struct LomoMappedFile {
    void* handle;      // OS-specific handle (HANDLE on Win, fd on POSIX)
    void* mapping;     // OS-specific mapping object (HANDLE on Win, NULL on POSIX)
    const void* data;  // Pointer to mapped memory
    size_t size;       // Size of mapped memory
} LomoMappedFile;

int lomo_mmap_open(const char* filename, LomoMappedFile* mf);
void lomo_mmap_close(LomoMappedFile* mf);

// Compression
typedef enum {
    LOMO_HAL_COMPRESS_XPRESS = 1
} LomoHALCompressionAlg;

void* lomo_compressor_open(LomoHALCompressionAlg alg);
void lomo_compressor_close(void* handle);
int lomo_compress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_capacity, size_t* compressed_size);

void* lomo_decompressor_open(LomoHALCompressionAlg alg);
void lomo_decompressor_close(void* handle);
int lomo_decompress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_size, size_t* decompressed_size);

#endif // LOMO_OS_H
