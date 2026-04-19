#include "lomo_os.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#include <compressapi.h>
#include <lz4.h>
#include <stdlib.h>

void* lomo_aligned_malloc(size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}

void* lomo_aligned_realloc(void* ptr, size_t size, size_t alignment) {
    return _aligned_realloc(ptr, size, alignment);
}

void lomo_aligned_free(void* ptr) {
    _aligned_free(ptr);
}

void lomo_mutex_init(LomoMutex* mutex) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    if (cs) {
        InitializeCriticalSection(cs);
        mutex->handle = cs;
    }
}

void lomo_mutex_destroy(LomoMutex* mutex) {
    if (mutex->handle) {
        DeleteCriticalSection((CRITICAL_SECTION*)mutex->handle);
        free(mutex->handle);
        mutex->handle = NULL;
    }
}

void lomo_mutex_lock(LomoMutex* mutex) {
    if (mutex->handle) EnterCriticalSection((CRITICAL_SECTION*)mutex->handle);
}

void lomo_mutex_unlock(LomoMutex* mutex) {
    if (mutex->handle) LeaveCriticalSection((CRITICAL_SECTION*)mutex->handle);
}

int lomo_mmap_open(const char* filename, LomoMappedFile* mf) {
    mf->handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mf->handle == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER fs;
    if (!GetFileSizeEx(mf->handle, &fs)) {
        CloseHandle(mf->handle);
        return 0;
    }
    mf->size = (size_t)fs.QuadPart;

    mf->mapping = CreateFileMapping(mf->handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mf->mapping) {
        CloseHandle(mf->handle);
        return 0;
    }

    mf->data = MapViewOfFile(mf->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!mf->data) {
        CloseHandle(mf->mapping);
        CloseHandle(mf->handle);
        return 0;
    }

    return 1;
}

void lomo_mmap_close(LomoMappedFile* mf) {
    if (mf->data) UnmapViewOfFile(mf->data);
    if (mf->mapping) CloseHandle(mf->mapping);
    if (mf->handle && mf->handle != INVALID_HANDLE_VALUE) CloseHandle(mf->handle);
    mf->data = NULL;
    mf->mapping = NULL;
    mf->handle = NULL;
}

LomoFile* lomo_file_open_write(const char* path) {
    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;
    LomoFile* lf = (LomoFile*)malloc(sizeof(LomoFile));
    lf->handle = hFile;
    lf->extra = NULL;
    return lf;
}

int lomo_file_write_async(LomoFile* file, uint64_t offset, const void* data, size_t size) {
    OVERLAPPED ol = {0};
    ol.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ol.OffsetHigh = (DWORD)(offset >> 32);
    DWORD written = 0;
    if (WriteFile((HANDLE)file->handle, data, (DWORD)size, &written, &ol)) {
        return 1;
    }
    return 0;
}

int lomo_file_flush_and_close(LomoFile* file) {
    if (!file) return 0;
    FlushFileBuffers((HANDLE)file->handle);
    CloseHandle((HANDLE)file->handle);
    free(file);
    return 1;
}

void* lomo_compressor_open(LomoHALCompressionAlg alg) {
    if (alg == LOMO_HAL_COMPRESS_LZ4) return (void*)2; 

    COMPRESSOR_HANDLE handle = NULL;
    DWORD win_alg = (alg == LOMO_HAL_COMPRESS_XPRESS) ? COMPRESS_ALGORITHM_XPRESS : 0;
    if (win_alg && CreateCompressor(win_alg, NULL, &handle)) {
        return handle;
    }
    return NULL;
}

void lomo_compressor_close(void* handle) {
    if (handle == (void*)2) return;
    if (handle) CloseCompressor((COMPRESSOR_HANDLE)handle);
}

int lomo_compress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_capacity, size_t* compressed_size) {
    if (handle == (void*)2) {
        int res = LZ4_compress_default((const char*)src, (char*)dst, (int)src_size, (int)dst_capacity);
        if (res > 0) {
            *compressed_size = (size_t)res;
            return 1;
        }
        return 0;
    }
    SIZE_T comp_size = 0;
    if (Compress((COMPRESSOR_HANDLE)handle, (PVOID)src, src_size, dst, dst_capacity, &comp_size)) {
        *compressed_size = (size_t)comp_size;
        return 1;
    }
    return 0;
}

void* lomo_decompressor_open(LomoHALCompressionAlg alg) {
    if (alg == LOMO_HAL_COMPRESS_LZ4) return (void*)2;

    DECOMPRESSOR_HANDLE handle = NULL;
    DWORD win_alg = (alg == LOMO_HAL_COMPRESS_XPRESS) ? COMPRESS_ALGORITHM_XPRESS : 0;
    if (win_alg && CreateDecompressor(win_alg, NULL, &handle)) {
        return handle;
    }
    return NULL;
}

void lomo_decompressor_close(void* handle) {
    if (handle == (void*)2) return;
    if (handle) CloseDecompressor((DECOMPRESSOR_HANDLE)handle);
}

int lomo_decompress(void* handle, const void* src, size_t src_size, void* dst, size_t dst_size, size_t* decompressed_size) {
    if (handle == (void*)2) {
        int res = LZ4_decompress_safe((const char*)src, (char*)dst, (int)src_size, (int)dst_size);
        if (res >= 0) {
            *decompressed_size = (size_t)res;
            return 1;
        }
        return 0;
    }
    SIZE_T decomp_size = 0;
    if (Decompress((DECOMPRESSOR_HANDLE)handle, (PVOID)src, src_size, dst, dst_size, &decomp_size)) {
        *decompressed_size = (size_t)decomp_size;
        return 1;
    }
    return 0;
}
