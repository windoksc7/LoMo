#include "lomo_os.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

void* lomo_aligned_malloc(size_t size, size_t alignment) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return NULL;
}

void* lomo_aligned_realloc(void* ptr, size_t size, size_t alignment) {
    void* new_ptr = lomo_aligned_malloc(size, alignment);
    if (new_ptr && ptr) {
        // Warning: potential data loss if old size > new size or vice versa
        // but we don't have old_size here.
        memcpy(new_ptr, ptr, size); 
        lomo_aligned_free(ptr);
    }
    return new_ptr;
}

void lomo_aligned_free(void* ptr) {
    free(ptr);
}

void lomo_mutex_init(LomoMutex* mutex) {
    pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (m) {
        pthread_mutex_init(m, NULL);
        mutex->handle = m;
    }
}

void lomo_mutex_destroy(LomoMutex* mutex) {
    if (mutex->handle) {
        pthread_mutex_destroy((pthread_mutex_t*)mutex->handle);
        free(mutex->handle);
        mutex->handle = NULL;
    }
}

void lomo_mutex_lock(LomoMutex* mutex) {
    if (mutex->handle) pthread_mutex_lock((pthread_mutex_t*)mutex->handle);
}

void lomo_mutex_unlock(LomoMutex* mutex) {
    if (mutex->handle) pthread_mutex_unlock((pthread_mutex_t*)mutex->handle);
}

int lomo_mmap_open(const char* filename, LomoMappedFile* mf) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }
    mf->size = st.st_size;
    mf->handle = (void*)(intptr_t)fd;
    mf->mapping = NULL;

    mf->data = mmap(NULL, mf->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mf->data == MAP_FAILED) {
        close(fd);
        return 0;
    }

    return 1;
}

void lomo_mmap_close(LomoMappedFile* mf) {
    if (mf->data && mf->data != MAP_FAILED) {
        munmap((void*)mf->data, mf->size);
    }
    if (mf->handle) {
        close((int)(intptr_t)mf->handle);
    }
    mf->data = NULL;
    mf->handle = NULL;
}

// Compression stubs for POSIX (LZ4/ZSTD to be added later)
void* lomo_compressor_open(LomoHALCompressionAlg alg) { (void)alg; return NULL; }
void lomo_compressor_close(void* handle) { (void)handle; }
int lomo_compress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_capacity, size_t* compressed_size) {
    (void)handle; (void)src; (void)src_size; (void)dst; (void)dst_capacity; (void)compressed_size;
    return 0;
}

void* lomo_decompressor_open(LomoHALCompressionAlg alg) { (void)alg; return NULL; }
void lomo_decompressor_close(void* handle) { (void)handle; }
int lomo_decompress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_size, size_t* decompressed_size) {
    (void)handle; (void)src; (void)src_size; (void)dst; (void)dst_size; (void)decompressed_size;
    return 0;
}
