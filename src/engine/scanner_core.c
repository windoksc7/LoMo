#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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

typedef struct {
    const char* data;
    size_t size;
#if defined(_WIN32)
    HANDLE hFile;
    HANDLE hMapping;
#else
    int fd;
#endif
} MappedFile;

void unmap_file(MappedFile* mf) {
    if (!mf) return;

#if defined(_WIN32)
    if (mf->data) UnmapViewOfFile(mf->data);
    if (mf->hMapping) CloseHandle(mf->hMapping);
    if (mf->hFile) CloseHandle(mf->hFile);
#else
    // 리눅스/맥: 구조체에 담아뒀던 mf->fd를 여기서 사용합니다!
    if (mf->data && mf->data != MAP_FAILED) {
        munmap(mf->data, mf->size); 
    }
    if (mf->fd >= 0) {
        close(mf->fd); // 드디어 fd를 닫습니다.
    }
#endif
    free(mf); // 마지막으로 구조체 자체를 메모리에서 해제
}

// [공통 함수] 파일을 메모리에 매핑하고 정보를 구조체에 담아 반환
MappedFile* map_file_to_memory(const char* filename) {
    MappedFile* mf = (MappedFile*)malloc(sizeof(MappedFile));
    if (!mf) return NULL;

#if defined(_WIN32)
    mf->hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (mf->hFile == INVALID_HANDLE_VALUE) { free(mf); return NULL; }
    
    LARGE_INTEGER li;
    GetFileSizeEx(mf->hFile, &li);
    mf->size = (size_t)li.QuadPart;
    
    mf->hMapping = CreateFileMapping(mf->hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    mf->data = (const char*)MapViewOfFile(mf->hMapping, FILE_MAP_READ, 0, 0, 0);
#else
    mf->fd = open(filename, O_RDONLY);
    if (mf->fd == -1) { free(mf); return NULL; }
    
    struct stat st;
    fstat(mf->fd, &st);
    mf->size = (size_t)st.st_size;
    
    mf->data = (const char*)mmap(NULL, mf->size, PROT_READ, MAP_PRIVATE, mf->fd, 0);
#endif

    if (!mf->data) {
        // 에러 발생 시 정리 로직...
        return NULL;
    }
    return mf;
}


// --- [OS/CPU별 최적화 필터 함수] ---
static inline int check_match_32byte(const char* ptr, char c1, char c2) {
#if defined(__x86_64__) || defined(_M_X64)
    // 인텔/AMD AVX2 (256비트)
    __m256i chunk1 = _mm256_loadu_si256((const __m256i*)ptr);
    __m256i chunk2 = _mm256_loadu_si256((const __m256i*)(ptr + 1));
    return _mm256_movemask_epi8(_mm256_and_si256(
        _mm256_cmpeq_epi8(chunk1, _mm256_set1_epi8(c1)),
        _mm256_cmpeq_epi8(chunk2, _mm256_set1_epi8(c2))
    ));
#elif defined(__aarch64__) || defined(_M_ARM64)
    // Apple Silicon / ARM NEON (128비트)
    uint8x16_t chunk1 = vld1q_u8((const uint8_t*)ptr);
    uint8x16_t chunk2 = vld1q_u8((const uint8_t*)(ptr + 1));
    uint8x16_t cmp = vandq_u8(vceqq_u8(chunk1, vdupq_n_u8(c1)), 
                              vceqq_u8(chunk2, vdupq_n_u8(c2)));
    // NEON은 mask가 없으므로 합산이 0보다 크면 일치하는 지점이 있는 것
    return (vaddvq_u8(cmp) > 0); 
#else
    // 가속기가 없는 환경 (일반 C)
    return (*ptr == c1 && *(ptr+1) == c2);
#endif
}

void run_analysis(const char* data, size_t fileSize) {
    if (!data || fileSize == 0) return; // 방어 코드 추가

    const char* ptr = data;
    const char* end = data + fileSize;

    printf("--- analysis (file size: %.2f GB) ---\n", (double)fileSize / (1024*1024*1024));

    clock_t start = clock();
    uint64_t hacker_ips = 0, admin_hits = 0, env_leaks = 0;

    // 3. Single Pass Scan
    while (ptr <= end - 33) {
        // 해커 IP 필터링 (13...)
        if (check_match_32byte(ptr, '1', '3')) {
            if (memcmp(ptr, "13.37.13.37", 11) == 0) {
                hacker_ips++;
                ptr += 10; // 찾은 후 패턴 길이만큼 대략 점프 (성능 최적화)
            }
        }
        // 어드민 페이지 필터링 (/a...)
        else if (check_match_32byte(ptr, '/', 'a')) {
            if (memcmp(ptr, "/admin", 6) == 0) {
                admin_hits++;
                ptr += 5;
            }
        }
        // 환경변수 필터링 (/. ...)
        else if (check_match_32byte(ptr, '/', '.')) {
            if (memcmp(ptr, "/.env", 5) == 0) {
                env_leaks++;
                ptr += 4;
            }
        }
        ptr++; // 기본 1바이트 전진 (정확도 보장)
    }
    
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("1. hacker IP (13.37.13.37) : %" PRIu64 " count\n", hacker_ips);
    printf("2. admin page open check: %" PRIu64 " count\n", admin_hits);
    printf("3. .env check : %" PRIu64 " count\n", env_leaks);
    printf("------------------------------------------\n");
    printf("time: %.4f sec (speed: %.2f GB/s)\n", elapsed, (fileSize / 1024.0 / 1024.0 / 1024.0) / elapsed);
}

int dummy_main() {
    MappedFile* logFile = map_file_to_memory("dummy_web.log");
    if (!logFile) {
        printf("Log file mapping failed.\n");
        return 1;
    }

    run_analysis(logFile->data, logFile->size);

    // [수정] unmap_file 하나로 깔끔하게 정리
    unmap_file(logFile);

    printf("\nPress Enter to exit...");
    rewind(stdin);
    getchar(); 
    return 0;
}