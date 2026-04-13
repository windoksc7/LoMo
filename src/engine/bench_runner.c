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
extern unsigned long long scan_memory_avx2(const unsigned char* data, size_t size, unsigned char target);

// OS 통합 파일 매핑 로직 [cite: 467]
typedef struct {
    void* addr;
    size_t size;
} MappedFile;

MappedFile get_mapped_file(const char* path) {
    MappedFile mf = {NULL, 0};
#ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return mf;
    LARGE_INTEGER fs; GetFileSizeEx(hFile, &fs); mf.size = (size_t)fs.QuadPart;
    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    mf.addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) return mf;
    struct stat st; fstat(fd, &st); mf.size = st.st_size;
    mf.addr = mmap(NULL, mf.size, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
    return mf;
}

void execute_benchmark(const char* filepath, int target_gb) {
    MappedFile mf = get_mapped_file(filepath);
    if (!mf.addr) {
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
        total_hits += scan_memory_avx2(mf.addr, mf.size, 0x0A); // 원본 소스 호출
        processed += mf.size;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("[%dGB] 소요시간: %.4fs | 속도: %.2fGB/s | 결과: %llu lines\n", 
            target_gb, elapsed, (double)target_gb / elapsed, total_hits);
}

int main() {
    const char* test_log = "dummy_web.log"; 
    printf("K-Scanner 3-OS Performance Verification\n");

    // Phase 1: 1, 5, 10기가 선형 테스트 
    execute_benchmark(test_log, 1);
    execute_benchmark(test_log, 5);
    execute_benchmark(test_log, 10);

    return 0;
}