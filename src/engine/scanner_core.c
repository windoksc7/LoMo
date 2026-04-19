#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "lomo_os.h"

#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h> // Intel/AMD용 AVX2
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>  // Apple/ARM용 NEON
#endif

typedef LomoMappedFile MappedFile;

void unmap_file(MappedFile* mf) {
    if (!mf) return;
    lomo_mmap_close(mf);
    free(mf); // 마지막으로 구조체 자체를 메모리에서 해제
}

// [공통 함수] 파일을 메모리에 매핑하고 정보를 구조체에 담아 반환
MappedFile* map_file_to_memory(const char* filename) {
    MappedFile* mf = (MappedFile*)malloc(sizeof(MappedFile));
    if (!mf) return NULL;

    if (!lomo_mmap_open(filename, mf)) {
        free(mf);
        return NULL;
    }
    return mf;
}


#include "simd_filter.h"

void run_analysis(const char* data, size_t fileSize) {
    if (!data || fileSize == 0) return; // 방어 코드 추가

    printf("--- analysis (file size: %.2f GB) ---\n", (double)fileSize / (1024*1024*1024));

    clock_t start = clock();
    
    // Use the new modularized SIMD count function
    uint64_t hacker_ips = lomo_simd_count_matches(data, fileSize, "13.37.13.37", 11);
    uint64_t admin_hits = lomo_simd_count_matches(data, fileSize, "/admin", 6);
    uint64_t env_leaks = lomo_simd_count_matches(data, fileSize, "/.env", 5);
    
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("1. hacker IP (13.37.13.37) : %" PRIu64 " count\n", hacker_ips);
    printf("2. admin page open check: %" PRIu64 " count\n", admin_hits);
    printf("3. .env check : %" PRIu64 " count\n", env_leaks);
    printf("------------------------------------------\n");
    printf("time: %.4f sec (speed: %.2f GB/s)\n", elapsed, (fileSize / 1024.0 / 1024.0 / 1024.0) / elapsed);
}

int main() {
    const char* targets[] = {"dummy_web.log", "dummy_web_5.log", "dummy_web_10.log"};
    
    for (int i = 0; i < 3; i++) {
        MappedFile* logFile = map_file_to_memory(targets[i]);
        if (!logFile) {
            printf("Skipping %s (not found or mapping failed).\n", targets[i]);
            continue;
        }

        run_analysis(logFile->data, logFile->size);
        unmap_file(logFile);
    }

    // printf("\nPress Enter to exit...");
    // rewind(stdin);
    // getchar(); 
    return 0;
}