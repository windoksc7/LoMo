#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <immintrin.h> // SIMD(AVX2) 내장 함수 헤더

void scan_log_mmap(const char* filename) {
    // 1. 파일 열기
    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { printf("file open fail\n"); return; }

    // 2. 파일 크기 확인
    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);

    // 3. 파일 매핑 객체 생성 (mmap 준비)
    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) { CloseHandle(hFile); return; }

    // 4. 메모리 주소 공간에 파일 매핑
    const char* data = (const char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!data) { CloseHandle(hMapping); CloseHandle(hFile); return; }

    printf("scan start (size: %.2f GB)...\n", (double)fileSize.QuadPart / (1024*1024*1024));

    // 5. 초고속 스캔 로직 (여기가 ksc님의 기술적 승부처)
    clock_t start = clock();
    uint64_t count = 0;
    const char* ptr = data;
    const char* end = data + fileSize.QuadPart;

    // 32바이트씩 묶어서 처리할 바구니(Vector) 생성
    // '[' 문자(0x5B)로 가득 채운 256비트(32바이트) 레지스터
    __m256i target = _mm256_set1_epi8('[');

    while (ptr <= end - 32) {
        // 1. 메모리에서 32바이트를 한 번에 로드
        __m256i chunk = _mm256_loadu_si256((const __m256i*)ptr);

        // 2. target('[')과 chunk(데이터)를 한 번에 비교 (일치하면 0xFF, 아니면 0x00)
        __m256i comparison = _mm256_cmpeq_epi8(chunk, target);

        // 3. 비교 결과(비트마스크)를 32비트 정수로 추출
        int mask = _mm256_movemask_epi8(comparison);

        // 4. mask에서 1로 세팅된 비트의 개수(일치하는 글자 수)를 카운트
        if (mask != 0) {
            count += __popcnt(mask); // 정수 내 비트 개수를 세는 하드웨어 명령
        }

        ptr += 32; // 32바이트 점프!
    }

    // 남은 자투리 데이터 처리 (32바이트 미만)
    while (ptr < end) {
        if (*ptr == '[') count++;
        ptr++;
    }

    clock_t end_time = clock();
    double elapsed = (double)(end_time - start) / CLOCKS_PER_SEC;

    // 6. 결과 출력 및 해제
    printf("scan complete! found '[' count: %llu\n", count);
    printf("time: %.4f초\n", elapsed);
    printf("speed: %.2f GB/s\n", ((double)fileSize.QuadPart / (1024*1024*1024)) / elapsed);

    UnmapViewOfFile(data);
    CloseHandle(hMapping);
    CloseHandle(hFile);
}

int main() {
    scan_log_mmap("dummy_web.log");

    printf("\nexit for Enter...");
    getchar(); // 사용자 입력이 있을 때까지 대기
    getchar();
    return 0;
}