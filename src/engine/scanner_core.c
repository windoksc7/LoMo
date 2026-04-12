#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <immintrin.h>

#if defined(_WIN32)
    // Windows: CreateFileMapping, MapViewOfFile
    #include <windows.h>
#elif defined(__apple__) || defined(__linux__)
    // macOS & Linux: mmap, POSIX 표준
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

// [공통 함수] OS와 상관없이 오직 '속도'에만 집중하는 스캔 로직
uint64_t core_scanner_logic(const char* data, size_t size) {
    uint64_t count = 0;
    const char* ptr = data;
    const char* end = data + size;

    // AVX2 SIMD 로직 (모든 x86_64 OS 공통)
    __m256i target = _mm256_set1_epi8('[');
    while (ptr <= end - 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)ptr);
        __m256i comparison = _mm256_cmpeq_epi8(chunk, target);
        int mask = _mm256_movemask_epi8(comparison);
        if (mask != 0) count += __popcnt(mask);
        ptr += 32;
    }

    // 자투리 처리
    while (ptr < end) {
        if (*ptr == '[') count++;
        ptr++;
    }
    return count;
}

void scan_log_mmap(const char* filename) {
    const char* data = NULL;
    size_t fileSize = 0;

#if defined(_WIN32)
    // Windows mmap
    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER li; GetFileSizeEx(hFile, &li); fileSize = li.QuadPart;
    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    data = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
#else
    // POSIX (Linux/macOS) mmap
    int fd = open(filename, O_RDONLY);
    struct stat st; fstat(fd, &st); fileSize = st.st_size;
    data = (const char*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
#endif

    // 공통 함수 호출
    clock_t start = clock();
    uint64_t total_found = core_scanner_logic(data, fileSize);
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    printf("Result: %llu found in %.4f sec (%.2f GB/s)\n", total_found, elapsed, (fileSize/1024.0/1024.0/1024.0)/elapsed);

#if defined(_WIN32)
    UnmapViewOfFile(data); CloseHandle(hMapping); CloseHandle(hFile);
#else
    munmap((void*)data, fileSize); close(fd);
#endif
}

int main() {
    scan_log_mmap("dummy_web.log");

    printf("\nexit for Enter...");
    getchar(); // 사용자 입력이 있을 때까지 대기
    getchar();
    return 0;
}