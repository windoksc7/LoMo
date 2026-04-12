#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TARGET_SIZE_GB 1.0
#define BUFFER_SIZE (1024 * 1024) // 1MB 버퍼

int main() {
    const char *filename = "dummy_web.log";
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("파일 열기 실패");
        return 1;
    }

    const char *methods[] = {"GET", "POST", "PUT", "DELETE"};
    const char *paths[] = {"/index.html", "/api/login", "/static/css/main.css", "/api/v1/user/profile", "/images/logo.png"};
    const int status_codes[] = {200, 201, 301, 404, 500, 502};

    char *buffer = malloc(BUFFER_SIZE);
    long long total_bytes = 0;
    long long target_bytes = (long long)(TARGET_SIZE_GB * 1024 * 1024 * 1024);

    srand(time(NULL));

    printf("1GB 로그 생성 시작: %s...\n", filename);

    int buf_pos = 0;
    while (total_bytes < target_bytes) {
        // 더미 로그 라인 생성 (Nginx Common Log Format 스타일)
        int len = sprintf(buffer + buf_pos, 
            "192.168.0.%d - - [12/Apr/2026:22:55:41 +0900] \"%s %s HTTP/1.1\" %d %d\n",
            rand() % 255, 
            methods[rand() % 4], 
            paths[rand() % 5], 
            status_codes[rand() % 6], 
            rand() % 5000);

        buf_pos += len;
        total_bytes += len;

        // 버퍼가 가득 차면 한 번에 쓰기 (I/O 최적화)
        if (buf_pos > BUFFER_SIZE - 512) {
            fwrite(buffer, 1, buf_pos, fp);
            buf_pos = 0;
            // 진행률 표시
            printf("\r생성 중... %.2f%%", (double)total_bytes / target_bytes * 100);
            fflush(stdout);
        }
    }

    if (buf_pos > 0) fwrite(buffer, 1, buf_pos, fp);

    fclose(fp);
    free(buffer);
    printf("\n완료! 생성된 파일 크기: %.2f GB\n", (double)total_bytes / (1024 * 1024 * 1024));

    return 0;
}