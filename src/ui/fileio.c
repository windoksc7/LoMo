#include "types.h" // 구조체 정의 포함 가정
#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void OpenAndAnalyzeFile(HWND hwnd, const char* filename) {
    // 1. 에디터 컨텍스트 가져오기
    EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!ctx || !ctx->doc) return;

    FILE* fp = fopen(filename, "r");
    if (fp == NULL) return;

    // 2. 기존 문서 내용 초기화 (새 파일을 열기 위해)
    // 간단하게 하기 위해 lineCount를 0으로 돌립니다.
    ctx->doc->lineCount = 0;
    ctx->curX = 0;
    ctx->curY = 0;

    char line[1024];
    int lineNum = 0;

    // main.c 내 OpenAndAnalyzeFile 함수 내부 수정
    while (fgets(line, sizeof(line), fp)) {
        if(strlen(line) > 0) {
            AddLine(ctx->doc);
            Line* targetLine = &ctx->doc->lines[ctx->doc->lineCount - 1];

            // [핵심 수정] CP_ACP 대신 CP_UTF8을 사용합니다.
            int wLen = MultiByteToWideChar(CP_UTF8, 0, line, -1, NULL, 0);
            
            if (wLen > targetLine->capacity) {
                targetLine->capacity = wLen + 32;
                targetLine->text = (wchar_t*)realloc(targetLine->text, sizeof(wchar_t) * targetLine->capacity);
            }

            // 여기서도 CP_UTF8로 변환
            MultiByteToWideChar(CP_UTF8, 0, line, -1, targetLine->text, targetLine->capacity);
            
            // 줄바꿈 제거 로직은 그대로 유지
            int len = wcslen(targetLine->text);
            while (len > 0 && (targetLine->text[len - 1] == L'\n' || targetLine->text[len - 1] == L'\r')) {
                targetLine->text[--len] = L'\0';
            }
            targetLine->length = len;
        }
    }
    fclose(fp);

    // 디버깅용 타이틀 바 출력
    char debugMsg[100];
    sprintf(debugMsg, "LoMo - Loaded Lines: %d", ctx->doc->lineCount);
    SetWindowTextA(hwnd, debugMsg);
    
    // 4. 화면을 다시 그리라고 명령! (이게 없으면 로드해도 안 보입니다)
    InvalidateRect(hwnd, NULL, TRUE);
}