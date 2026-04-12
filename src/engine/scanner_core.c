#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>
#include <string.h>

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

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

// --- [OS 추상화 레이어] ---
#if defined(_WIN32)
    #include <windows.h>
    #include <intrin.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define __popcnt __builtin_popcount
#endif

// --- [공통 스캔 로직] ---
// 특정 패턴(IP, 경로 등)이 로그에 몇 번 등장하는지 0.1초 만에 찾아냅니다.
uint64_t count_pattern(const char* data, size_t size, const char* pattern) {
    uint64_t count = 0;
    size_t pattern_len = strlen(pattern);
    if (pattern_len < 2) return count_pattern(data, size, pattern); // 1글자면 기존 방식

    const char* ptr = data;
    const char* end = data + size;
    
    // 첫 두 글자를 타겟으로 설정 (예: '1', '3')
    char c1 = pattern[0];
    char c2 = pattern[1];

#if defined(__x86_64__) || defined(_M_X64)
    __m256i target1 = _mm256_set1_epi8(c1);
    __m256i target2 = _mm256_set1_epi8(c2);
    
    while (ptr <= end - 33) { // 2바이트 확인을 위해 33바이트 여유
        __m256i chunk1 = _mm256_loadu_si256((const __m256i*)ptr);
        __m256i chunk2 = _mm256_loadu_si256((const __m256i*)(ptr + 1)); // 한 칸 밀어서 로드
        
        // chunk1에는 '1'이 있고, 동시에 chunk2에는 '3'이 있는 위치 찾기
        __m256i cmp1 = _mm256_cmpeq_epi8(chunk1, target1);
        __m256i cmp2 = _mm256_cmpeq_epi8(chunk2, target2);
        
        // 두 조건이 모두 만족하는 지점만 마스크로 생성
        int mask = _mm256_movemask_epi8(_mm256_and_si256(cmp1, cmp2));

        if (mask != 0) {
            for (int i = 0; i < 32; i++) {
                if ((mask >> i) & 1) {
                    // 이제 진짜 '13'으로 시작하는 지점만 memcmp 실행!
                    if (memcmp(ptr + i, pattern, pattern_len) == 0) {
                        count++;
                    }
                }
            }
        }
        ptr += 32;
    }
#endif

    // 남은 부분 처리
    while (ptr < end - pattern_len) {
        if (*ptr == c1 && memcmp(ptr, pattern, pattern_len) == 0) {
            count++;
        }
        ptr++;
    }
    return count;
}

void run_analysis(const char* filename) {
    const char* data = NULL;
    size_t fileSize = 0;

    // 1. 먼저 파일을 열고 data와 fileSize를 가져옵니다.
#if defined(_WIN32)
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { printf("cannot find file.\n"); return; }
    LARGE_INTEGER li; GetFileSizeEx(hFile, &li); fileSize = (size_t)li.QuadPart;
    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    data = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
#else
    int fd = open(filename, O_RDONLY);
    if (fd == -1) { printf("cannot find file.\n"); return; }
    struct stat st; fstat(fd, &st); fileSize = (size_t)st.st_size;
    data = (const char*)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
#endif

    if (!data) { printf("memory mapping fail!\n"); return; }

    // 2. [중요] data와 fileSize가 결정된 '후'에 ptr과 end를 설정합니다.
    const char* ptr = data;
    const char* end = data + fileSize;

    printf("--- analysis (file size: %.2f GB) ---\n", (double)fileSize / (1024*1024*1024));

    clock_t start = clock();
    
    uint64_t hacker_ips = 0;
    uint64_t admin_hits = 0;
    uint64_t env_leaks = 0;

    // 3. Single Pass Scan (한 번에 3개 다 찾기)
    while (ptr <= end - 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)ptr);
        __m256i chunk_next = _mm256_loadu_si256((const __m256i*)(ptr + 1));

        // 1. 해커 IP 필터링: '1'과 '3'이 연속되는 지점만!
        int mask_hacker = _mm256_movemask_epi8(_mm256_and_si256(
            _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('1')),
            _mm256_cmpeq_epi8(chunk_next, _mm256_set1_epi8('3'))
        ));

        // 2. 경로 필터링: '/'와 'a'가 연속되거나(admin), '/'와 '.'이 연속되는(.env) 지점만!
        int mask_admin = _mm256_movemask_epi8(_mm256_and_si256(
            _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('/')),
            _mm256_cmpeq_epi8(chunk_next, _mm256_set1_epi8('a'))
        ));
        
        int mask_env = _mm256_movemask_epi8(_mm256_and_si256(
            _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('/')),
            _mm256_cmpeq_epi8(chunk_next, _mm256_set1_epi8('.'))
        ));

        // 이제 각 마스크가 있을 때만 해당 항목을 체크합니다.
        if (mask_hacker) { hacker_ips++; }
        if (mask_admin) { admin_hits++; }
        if (mask_env) { env_leaks++; }

        ptr += 32;
    }
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    // --- [Step 3: 결과 출력] ---
    printf("1. hacker IP (13.37.13.37) : %" PRIu64 " count\n", hacker_ips);
    printf("2. admin page open check: %" PRIu64 " count\n", admin_hits);
    printf("3. .env check : %" PRIu64 " count\n", env_leaks);
    printf("------------------------------------------\n");
    printf("time: %.4f sec (speed: %.2f GB/s)\n", elapsed, (fileSize / 1024.0 / 1024.0 / 1024.0) / elapsed);

    // --- [Step 4: 자원 해제] ---
#if defined(_WIN32)
    UnmapViewOfFile(data); CloseHandle(hMapping); CloseHandle(hFile);
#else
    munmap((void*)data, fileSize); close(fd);
#endif
}

int main() {
    run_analysis("dummy_web.log");

    printf("enter to closed\n");
    rewind(stdin);
    getchar(); // 사용자 입력이 있을 때까지 대기
    getchar();
    return 0;
}