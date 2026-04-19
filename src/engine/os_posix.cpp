#include "lomo_os.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <lz4.h>

#ifdef __linux__
#include <liburing.h>
#define LOMO_URING_ENTRIES 64
#endif

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

LomoFile* lomo_file_open_write(const char* path) {
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef __linux__
    // Bypassing page cache for maximum performance
    flags |= O_DIRECT;
#endif
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;

    LomoFile* lf = (LomoFile*)malloc(sizeof(LomoFile));
    lf->handle = (void*)(intptr_t)fd;
    lf->extra = NULL;

#ifdef __linux__
    struct io_uring* ring = (struct io_uring*)malloc(sizeof(struct io_uring));
    if (io_uring_queue_init(LOMO_URING_ENTRIES, ring, 0) == 0) {
        lf->extra = ring;
    } else {
        free(ring);
    }
#endif
    return lf;
}

int lomo_file_write_async(LomoFile* file, uint64_t offset, const void* data, size_t size) {
    int fd = (int)(intptr_t)file->handle;
#ifdef __linux__
    struct io_uring* ring = (struct io_uring*)file->extra;
    if (ring) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (!sqe) {
            io_uring_submit(ring);
            sqe = io_uring_get_sqe(ring);
        }
        if (sqe) {
            io_uring_prep_write(sqe, fd, data, (unsigned)size, offset);
            return 1;
        }
    }
#endif
    // Fallback for non-Linux or ring allocation failure
    if (pwrite(fd, data, size, offset) == (ssize_t)size) {
        return 1;
    }
    return 0;
}

int lomo_file_flush_and_close(LomoFile* file) {
    if (!file) return 0;
    int fd = (int)(intptr_t)file->handle;
#ifdef __linux__
    struct io_uring* ring = (struct io_uring*)file->extra;
    if (ring) {
        io_uring_submit_and_wait(ring, 0); // Flush all pending writes
        io_uring_queue_exit(ring);
        free(ring);
    }
#endif
    fsync(fd);
    close(fd);
    free(file);
    return 1;
}

void* lomo_compressor_open(LomoHALCompressionAlg alg) {
    if (alg == LOMO_HAL_COMPRESS_LZ4) return (void*)2;
    return NULL;
}

void lomo_compressor_close(void* handle) {
    (void)handle;
}

int lomo_compress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_capacity, size_t* compressed_size) {
    if (handle == (void*)2) {
        int res = LZ4_compress_default((const char*)src, (char*)dst, (int)src_size, (int)dst_capacity);
        if (res > 0) {
            *compressed_size = (size_t)res;
            return 1;
        }
    }
    return 0;
}

void* lomo_decompressor_open(LomoHALCompressionAlg alg) {
    if (alg == LOMO_HAL_COMPRESS_LZ4) return (void*)2;
    return NULL;
}

void lomo_decompressor_close(void* handle) {
    (void)handle;
}

int lomo_decompress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_size, size_t* decompressed_size) {
    if (handle == (void*)2) {
        int res = LZ4_decompress_safe((const char*)src, (char*)dst, (int)src_size, (int)dst_size);
        if (res >= 0) {
            *decompressed_size = (size_t)res;
            return 1;
        }
    }
    return 0;
}
