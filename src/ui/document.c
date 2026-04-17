#include "types.h"
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void LoadFileContent(Document* doc, const char* filename) {
    if (!doc || !filename) return;

    // 1. 기존 문서 데이터 초기화
    DestroyDocument(doc); 
    doc->lineCount = 0;
    doc->maxLines = 100; // 초기 여유분
    doc->lines = (Line*)calloc(doc->maxLines, sizeof(Line));

    // 2. Win32 API로 파일 열기
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, 
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0) {
        CloseHandle(hFile);
        AddLine(doc); // 빈 파일이면 빈 줄 하나 생성
        return;
    }

    // 3. 파일 전체를 메모리에 한 번에 로드 (8GB RAM의 여유 활용)
    char* buffer = (char*)malloc(fileSize + 1);
    DWORD bytesRead;
    if (buffer) {
        ReadFile(hFile, buffer, fileSize, &bytesRead, NULL);
        buffer[fileSize] = '\0';

        // 4. 줄 단위 파싱 (CRLF / LF 대응)
        char* lineStart = buffer;
        char* nextLine = NULL;

        while (lineStart < buffer + fileSize) {
            nextLine = strchr(lineStart, '\n');
            int rawLen = nextLine ? (int)(nextLine - lineStart) : (int)(buffer + fileSize - lineStart);
            
            // 줄 끝의 \r 제거
            int finalLen = rawLen;
            if (finalLen > 0 && lineStart[finalLen - 1] == '\r') finalLen--;

            // 새 줄 확보
            if (doc->lineCount >= doc->maxLines) AddLine(doc);
            Line* line = &doc->lines[doc->lineCount - 1];

            // ANSI -> UNICODE 변환하여 저장
            int wLen = MultiByteToWideChar(CP_ACP, 0, lineStart, finalLen, NULL, 0);
            line->capacity = wLen + 1;
            line->text = (wchar_t*)calloc(line->capacity, sizeof(wchar_t));
            MultiByteToWideChar(CP_ACP, 0, lineStart, finalLen, line->text, wLen);
            line->length = wLen;
            line->isDirty = TRUE; // 로드 즉시 분석 대상

            if (!nextLine) break;
            lineStart = nextLine + 1;
            AddLine(doc); // 다음 줄 준비
        }
        free(buffer);
    }

    CloseHandle(hFile);
}

Document* CreateDocument() {
    Document* doc = (Document*)malloc(sizeof(Document));
    if (!doc) return NULL;

    doc->lineCount = 0;
    doc->maxLines = 10; 
    
    // malloc 대신 calloc을 사용하면 모든 메모리가 0으로 초기화되어 안전합니다.
    doc->lines = (Line*)calloc(doc->maxLines, sizeof(Line));

    if (!doc->lines) {
        free(doc);
        return NULL;
    }

    // 첫 줄을 생성합니다.
    AddLine(doc); 
    return doc;
}

// [개선된 AddLine]
void AddLine(Document* doc) {
    // 용량이 꽉 찼다면 늘려준다 (동적 확장)
    if (doc->lineCount >= doc->maxLines) {
        int newMax = doc->maxLines + 50; // 50줄씩 확장
        Line* temp = (Line*)realloc(doc->lines, sizeof(Line) * newMax);
        if (temp) {
            doc->lines = temp;
            doc->maxLines = newMax;
            // 새로 할당된 영역을 0으로 초기화 (중요)
            memset(&doc->lines[doc->lineCount], 0, sizeof(Line) * (newMax - doc->lineCount));
        } else {
            return; 
        }
    }

    Line* newLine = &doc->lines[doc->lineCount];
    newLine->capacity = 128;
    newLine->length = 0;
    newLine->text = (wchar_t*)calloc(newLine->capacity, sizeof(wchar_t)); // malloc 대신 calloc 권장
    
    if (newLine->text) {
        doc->lineCount++;
    }
}

// [추가 권장: 메모리 해제 함수]
void DestroyDocument(Document* doc) {
    if (!doc) return;
    
    // 각 줄의 텍스트 메모리 해제
    for (int i = 0; i < doc->lineCount; i++) {
        if (doc->lines[i].text) {
            free(doc->lines[i].text);
        }
    }
    
    // 줄 배열 자체 해제
    if (doc->lines) {
        free(doc->lines);
    }
    
    // 구조체 초기화
    doc->lines = NULL;
    doc->lineCount = 0;
    doc->maxLines = 0;
}

// 모든 줄의 분석 데이터(Metadata)를 태초의 상태로 되돌립니다.
void ResetLineState(Line* line) {
    if (!line) return;

    line->vState.targetEnergy = -1;
    line->targetLine = -1;
    line->isDirty = FALSE;
    
    // 이름 버퍼들을 깨끗이 비웁니다. (memset은 1.3MB 사양에서도 매우 빠릅니다)
    memset(line->unitName, 0, sizeof(line->unitName));
    memset(line->targetName, 0, sizeof(line->targetName));
}