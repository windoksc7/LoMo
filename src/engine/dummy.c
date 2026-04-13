#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void generate_fake_logs(const char* filename, long long target_size_gb) {
    FILE* fp = fopen(filename, "w");
    if (!fp) return;

    const char* ips[] = {"192.168.1.10", "172.16.254.1", "10.0.0.15", "210.15.22.4", "1.23.45.67"};
    const char* methods[] = {"GET", "POST"};
    const char* paths[] = {"/index.html", "/shop/item?id=1", "/contact", "/api/v1/user", "/static/css/main.css"};
    const char* status[] = {"200", "200", "200", "301", "404", "500"}; // 200 비중을 높게
    
    // 수상한 데이터 (공격 패턴)
    const char* malicious_ips[] = {"13.37.13.37", "66.66.66.66"};
    const char* malicious_paths[] = {"/admin/config.php", "/.env", "/wp-login.php", "/etc/passwd"};

    long long current_size = 0;
    long long target_size = target_size_gb * 1024LL * 1024LL * 1024LL;
    int line_count = 0;

    printf("Generating %lldGB of realistic logs...\n", target_size_gb);

    while (current_size < target_size) {
        char buffer[256];
        int len;

        // 5,000줄마다 하나씩 수상한 접근 생성
        if (line_count % 5000 == 0) {
            len = sprintf(buffer, "[2026-04-13 12:00:00] %s %s %s 404\n", 
                          malicious_ips[rand() % 2], methods[rand() % 2], malicious_paths[rand() % 4]);
        } else {
            len = sprintf(buffer, "[2026-04-13 12:00:00] %s %s %s %s\n", 
                          ips[rand() % 5], methods[rand() % 2], paths[rand() % 5], status[rand() % 6]);
        }

        fputs(buffer, fp);
        current_size += len;
        line_count++;
    }

    fclose(fp);
    printf("Complete! Total lines: %d\n", line_count);
}

int main() {
    srand((unsigned int)time(NULL));
    generate_fake_logs("dummy_web.log", 1); // 1GB 생성
    generate_fake_logs("dummy_web_5.log", 5); 
    generate_fake_logs("dummy_web_10.log", 10); 
    return 0;
}