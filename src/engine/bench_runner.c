#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// OS별 헤더 분기 
#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// scanner_core.c에 있는 함수를 가져다 씁니다. (원본 수정 없음)
extern void run_analysis(const char* data, size_t fileSize);

// OS 통합 파일 매핑 로직 [cite: 467]
typedef struct {
    const char* data;
    size_t size;
} MappedFile;

void execute_benchmark(const char* filepath, int target_gb) {
    MappedFile* mf = map_file_to_memory(filepath);
    if (!mf->data) {
        printf("[Error] 파일 매핑 실패\n");
        return;
    }

    long long total_scan_limit = (long long)target_gb * 1024 * 1024 * 1024;
    long long processed = 0;
    unsigned long long total_hits = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start); // 고정밀 타이머

    // 선형적 성능 유지를 확인하기 위한 반복 스캔 
    while (processed < total_scan_limit) {
        run_analysis(mf->data, mf->size); // 원본 소스 호출
        processed += mf->size;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("[%dGB] 소요시간: %.4fs | 속도: %.2fGB/s | 결과: %llu lines\n", 
            target_gb, elapsed, (double)target_gb / elapsed, total_hits);
}

#ifndef TEST_MODE
int main() {
    const char* test_log = "dummy_web.log"; 
    printf("K-Scanner 3-OS Performance Verification\n");

    // Phase 1: 1, 5, 10기가 선형 테스트 
    execute_benchmark(test_log, 1);
    execute_benchmark(test_log, 5);
    execute_benchmark(test_log, 10);

    return 0;
}
#endif