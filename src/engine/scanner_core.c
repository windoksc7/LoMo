#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h> // Intel/AMD용 AVX2
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>  // Apple/ARM용 NEON
#endif

#if defined(_WIN32)
    // Windows: CreateFileMapping, MapViewOfFile
    #include <windows.h>
    #include <intrin.h>
#else
    // macOS & Linux: mmap, POSIX 표준
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define __popcnt __builtin_popcount
#endif

// [공통 함수] OS와 상관없이 오직 '속도'에만 집중하는 스캔 로직
uint64_t core_scanner_logic(const char* data, size_t size) {
    uint64_t count = 0;
    const char* ptr = data;
    const char* end = data + size;
#if defined(__x86_64__) || defined(_M_X64)
    // --- [Intel/AMD] AVX2 로직 (32바이트씩) ---
    __m256i target = _mm256_set1_epi8('[');
    while (ptr <= end - 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)ptr);
        __m256i comparison = _mm256_cmpeq_epi8(chunk, target);
        int mask = _mm256_movemask_epi8(comparison);
        if (mask != 0) count += __popcnt(mask);
        ptr += 32;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    // --- [Apple Silicon/ARM] NEON 로직 (16바이트씩) ---
    uint8x16_t target = vdupq_n_u8('[');
    while (ptr <= end - 16) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)ptr);
        uint8x16_t comparison = vceqq_u8(chunk, target);
        // 비교 결과에서 1인 비트들만 모아서 카운트
        uint64_t high = vgetq_lane_u64(vreinterpretq_u64_u8(comparison), 1);
        uint64_t low = vgetq_lane_u64(vreinterpretq_u64_u8(comparison), 0);
        if (high || low) {
            // 이 부분은 단순화를 위해 바이트별 체크로 보완하거나 
            // 더 정교한 비트 카운팅 로직을 넣을 수 있습니다.
            for(int i=0; i<16; i++) if(ptr[i] == '[') count++;
        }
        ptr += 16;
    }
#endif

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

    printf("Result: %" PRIu64 " found in %.4f sec (%.2f GB/s)\n", total_found, elapsed, (fileSize/1024.0/1024.0/1024.0)/elapsed);
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