#pragma execution_character_set("utf-8")
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <dwmapi.h>
#include <string.h>
#include <imm.h>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"imm32.lib")

typedef struct {
    wchar_t* text;    // 실제 문자열이 저장된 동적 메모리 주소
    int length;       // 현재 줄에 타이핑된 글자 수
    int capacity;     // 현재 할당된 메모리 크기 (realloc 빈도를 줄이기 위함)
} Line;

// 전체 문서를 관리할 메인 구조체
typedef struct {
    Line* lines;      // Line 구조체들의 배열 (동적 할당)
    int totalLines;   // 현재 전체 줄 수
    int maxLines;     // 할당된 최대 줄 수
} Document;

HFONT hFont = NULL;
static int charWidth, charHeight;
static int curX = 0, curY = 0;
Document* doc;

void AddLine(Document* doc) {
    if (doc->totalLines >= doc->maxLines) {
        doc->maxLines *= 2;
        doc->lines = (Line*)realloc(doc->lines, sizeof(Line) * doc->maxLines);
    }
    
    // 새 줄 초기화 (기본 32자 공간 확보)
    Line* newLine = &doc->lines[doc->totalLines];
    newLine->capacity = 32;
    newLine->text = (wchar_t*)malloc(sizeof(wchar_t) * newLine->capacity);
    newLine->length = 0;
    newLine->text[0] = L'\0';
    
    doc->totalLines++;
}

void InsertChar(Line* line, wchar_t ch, int pos) {
    // 용량 체크 및 확장
    if (line->length + 1 >= line->capacity) {
        line->capacity *= 2;
        line->text = (wchar_t*)realloc(line->text, sizeof(wchar_t) * line->capacity);
        
    }
    
    // 단순 추가 로직 (중간 삽입은 memmove 필요)
    line->text[line->length] = ch;
    line->length++;
    line->text[line->length] = L'\0';
}

void DestroyDocument(Document* doc) {
    if (!doc) return;

    // 1. 각 줄(Line) 내부의 텍스트 메모리 해제
    for (int i = 0; i < doc->totalLines; i++) {
        if (doc->lines[i].text) {
            free(doc->lines[i].text);
        }
    }

    // 2. 줄 구조체 배열 해제
    if (doc->lines) {
        free(doc->lines);
    }

    // 3. 문서 구조체 자체 해제
    free(doc);
}

void SaveLomoFile(const wchar_t* filename, Document* doc) {
    HANDLE hFile = CreateFileW(filename, GENERIC_WRITE, 0, NULL, 
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        for (int i = 0; i < doc->totalLines; i++) {
            // 1. 현재 줄의 텍스트 쓰기
            WriteFile(hFile, doc->lines[i].text, 
                      (DWORD)(doc->lines[i].length * sizeof(wchar_t)), 
                      &bytesWritten, NULL);
            
            // 2. 줄바꿈 문자 (\r\n) 쓰기 (마지막 줄 제외 혹은 전체 적용)
            if (i < doc->totalLines - 1) {
                const wchar_t* newline = L"\r\n";
                WriteFile(hFile, newline, (DWORD)(2 * sizeof(wchar_t)), 
                          &bytesWritten, NULL);
            }
        }
        CloseHandle(hFile);
    }
}

// index 위치까지의 픽셀 길이를 계산하는 함수
int GetXPixelFromIndex(HDC hdc, wchar_t* text, int index) {
    SIZE size;
    GetTextExtentPoint32W(hdc, text, index, &size);
    return size.cx;
}

void LoadLomoFile(HWND hwnd, const wchar_t* filename, Document** docPtr) {
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, 0, NULL, 
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        // 기존 메모리 안전하게 해제
        DestroyDocument(*docPtr);

        // 새 문서 구조체 할당
        *docPtr = (Document*)malloc(sizeof(Document));
        (*docPtr)->maxLines = 100;
        (*docPtr)->lines = (Line*)malloc(sizeof(Line) * (*docPtr)->maxLines);
        (*docPtr)->totalLines = 0;
        
        AddLine(*docPtr);
        int targetLine = 0;

        DWORD fileSize = GetFileSize(hFile, NULL);
        wchar_t* tempBuf = (wchar_t*)malloc(fileSize + sizeof(wchar_t));
        DWORD bytesRead;
        
        ReadFile(hFile, tempBuf, fileSize, &bytesRead, NULL);
        int totalChars = bytesRead / sizeof(wchar_t);
        tempBuf[totalChars] = L'\0';

        for (int i = 0; i < totalChars; i++) {
            if (tempBuf[i] == L'\r') continue; // \r은 건너뜀
            if (tempBuf[i] == L'\n') {
                AddLine(*docPtr);
                targetLine++;
            } else {
                InsertChar(&(*docPtr)->lines[targetLine], tempBuf[i], (*docPtr)->lines[targetLine].length);
            }
        }

        free(tempBuf);
        CloseHandle(hFile);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

// WindowProc 위에 추가하거나 전역으로 선언하세요.
void UpdateCursorAndUI(HWND hwnd, Document* doc, int curX, int curY, HFONT hFont, int charHeight) {
    // 1. 현재 줄 데이터 가져오기
    wchar_t* currentText = doc->lines[curY].text;

    // 2. 픽셀 길이 계산
    SIZE size;
    HDC hdc = GetDC(hwnd);
    SelectObject(hdc, hFont);
    GetTextExtentPoint32W(hdc, currentText, curX, &size);
    ReleaseDC(hwnd, hdc);

    // 3. 커서 위치 설정
    SetCaretPos(size.cx + 10, curY * charHeight + 10);

    // 4. IME 위치 동기화
    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        COMPOSITIONFORM cf;
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = size.cx + 10;
        cf.ptCurrentPos.y = curY * charHeight + 10;
        ImmSetCompositionWindow(himc, &cf);
        ImmReleaseContext(hwnd, himc);
    }

    // 5. 화면 갱신 강제 발생 (매우 중요!)
    InvalidateRect(hwnd, NULL, TRUE); 
}

void HandleBackSpace(Document* doc, int* curX, int* curY) {
    if (*curX > 0) {
        // [시나리오 A] 한 줄 내부에서 삭제
        Line* line = &doc->lines[*curY];
        
        // 커서 앞의 글자를 삭제하고 뒤를 당김
        if (*curX < line->length) {
            memmove(&line->text[*curX - 1], &line->text[*curX], 
                    (line->length - *curX) * sizeof(wchar_t));
        }
        
        line->length--;
        line->text[line->length] = L'\0';
        (*curX)--;
    } 
    else if (*curY > 0) {
        // [시나리오 B] 윗줄과 합치기
        int prevIdx = *curY - 1;
        Line* prevLine = &doc->lines[prevIdx];
        Line* currLine = &doc->lines[*curY];
        
        int oldPrevLen = prevLine->length;
        int newTotalLen = prevLine->length + currLine->length;

        // 윗줄의 가용 공간이 충분한지 확인
        int needed = prevLine->length + currLine->length + 1;
        wchar_t* temp = (wchar_t*)realloc(prevLine->text, (newTotalLen + 1) * sizeof(wchar_t));
        if (temp) {
            prevLine->text = temp;
            prevLine->capacity = newTotalLen + 1;

            // 현재 줄의 내용을 윗줄 끝에 복사
            if (currLine->length > 0) {
                memcpy(&prevLine->text[oldPrevLen], currLine->text, 
                       currLine->length * sizeof(wchar_t));
            }
            prevLine->length = newTotalLen;
            prevLine->text[newTotalLen] = L'\0';

            // 현재 줄의 메모리 해제
            free(currLine->text);

            // Document의 lines 배열에서 현재 줄을 제거하고 뒤쪽 줄들을 위로 당김
            for (int i = *curY; i < doc->totalLines - 1; i++) {
                doc->lines[i] = doc->lines[i + 1];
            }
            doc->totalLines--;

            // 커서 위치를 윗줄의 합쳐진 지점으로 이동
            *curY = prevIdx;
            *curX = oldPrevLen;
        }
    }
}

void HandleEnter(Document* doc, int* curX, int* curY) {
    // 1. 새로운 줄 공간 확보 (배열 확장)
    if (doc->totalLines >= doc->maxLines) {
        int newMax = doc->maxLines * 2;
        Line* temp = (Line*)realloc(doc->lines, newMax * sizeof(Line));
        if (temp) {
            doc->lines = temp;
            doc->maxLines = newMax;
        }
    }

    // 2. 현재 줄 아래에 공간 만들기 (기존 줄들을 한 칸씩 뒤로 밀기)
    for (int i = doc->totalLines; i > *curY + 1; i--) {
        doc->lines[i] = doc->lines[i - 1];
    }
    doc->totalLines++;

    Line* currentLine = &doc->lines[*curY];
    Line* newLine = &doc->lines[*curY + 1];

    // 3. 새 줄 메모리 할당 및 데이터 이동
    int moveLen = currentLine->length - *curX;
    newLine->length = moveLen;
    newLine->capacity = moveLen + 10; // 여유 공간 확보
    newLine->text = (wchar_t*)malloc(newLine->capacity * sizeof(wchar_t));

    if (moveLen > 0) {
        // 커서 뒤의 내용을 새 줄로 복사
        memcpy(newLine->text, &currentLine->text[*curX], moveLen * sizeof(wchar_t));
    }
    newLine->text[moveLen] = L'\0';

    // 4. 원래 줄 자르기
    currentLine->length = *curX;
    currentLine->text[*curX] = L'\0';

    // 5. 커서 위치 갱신
    (*curY)++;
    (*curX) = 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            DestroyDocument(doc);
            if(hFont) DeleteObject(hFont);
            PostQuitMessage(0);
            return 0;
        case WM_IME_COMPOSITION:
            if (lParam & GCS_RESULTSTR) { // 글자가 완성되었을 때
                HIMC himc = ImmGetContext(hwnd);
                wchar_t wBuf[2]={0}; 
                ImmGetCompositionStringW(himc, GCS_RESULTSTR, wBuf, sizeof(wBuf));
                
                // 1. 버퍼에 넣기
                InsertChar(&doc->lines[curY], wBuf[0], curX);
                curX++;
                
                // 2. 화면 갱신 및 커서 업데이트 (WM_CHAR에 있는 로직을 함수로 빼서 호출 추천)
                // WM_CHAR 내부
                UpdateCursorAndUI(hwnd, doc, curX, curY, hFont, charHeight); // ✅ 정상 작동
                ImmReleaseContext(hwnd, himc);
            }
            return 0; // IME 메시지를 직접 처리했음을 알림

        case WM_CHAR:
            // 영문 및 제어문자(BackSpace, Enter)만 여기서 처리
            if (wParam == VK_BACK) { 
                HandleBackSpace(doc, &curX, &curY);
            }else if (wParam == VK_RETURN) {
                // 정교화된 엔터 로직 호출
                HandleEnter(doc, &curX, &curY);
            }
            else if (wParam >= 32 && wParam < 127) { // 표준 아스키 영역만
                InsertChar(&doc->lines[curY], (wchar_t)wParam, curX);
                curX++;
            }
            // WM_CHAR 내부
            UpdateCursorAndUI(hwnd, doc, curX, curY, hFont, charHeight); // ✅ 정상 작동
            return 0;
        
        case WM_CREATE:{
            hFont = CreateFontW(20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");

            CreateCaret(hwnd, (HBITMAP)NULL,2,18);
            ShowCaret(hwnd);

            HDC hdc = GetDC(hwnd);
            SelectObject(hdc, hFont);
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm); // 폰트의 실제 수치 정보를 가져옴
            charWidth = tm.tmAveCharWidth; // 평균 가로 폭
            charHeight = tm.tmHeight;      // 세로 높이
            ReleaseDC(hwnd, hdc);
            doc = (Document*)malloc(sizeof(Document));
            doc->maxLines = 100; // 일단 100줄 공간 확보
            doc->lines = (Line*)malloc(sizeof(Line) * doc->maxLines);
            doc->totalLines = 0;
            
            AddLine(doc);
            curX = 0; curY = 0;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
            
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc,hFont);

            wchar_t debugMsg[50];
            wsprintfW(debugMsg, L"Lines: %d, curX: %d", doc->totalLines, curX);
            TextOutW(hdc, 10, 500, debugMsg, lstrlenW(debugMsg));

            // 3. 화면에 글자 그리기
            for (int i = 0; i < doc->totalLines; i++) {
                if (doc->lines[i].text != NULL) {
                    TextOutW(hdc, 10, 10 + (i * charHeight), 
                            doc->lines[i].text, doc->lines[i].length);
                }
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:{
            switch (wParam) {
                case VK_LEFT:
                    if (curX > 0) curX--;
                    else if (curY > 0) { // 윗줄 끝으로 이동
                        curY--;
                        curX = doc->lines[curY].length;
                    }
                    break;
                case VK_RIGHT:
                    if (curX < doc->lines[curY].length) curX++;
                    else if (curY < doc->totalLines - 1) { // 아랫줄 처음으로 이동
                        curY++;
                        curX = 0;
                    }
                    break;
                case VK_UP:
                    if (curY > 0) {
                        curY--;
                        // 윗줄이 현재 줄보다 짧으면 그 줄의 끝으로 보냄
                        if (curX > doc->lines[curY].length) curX = doc->lines[curY].length;
                    }
                    break;
                case VK_DOWN:
                    if (curY < doc->totalLines - 1) {
                        curY++;
                        // 아랫줄이 현재 줄보다 짧으면 그 줄의 끝으로 보냄
                        if (curX > doc->lines[curY].length) curX = doc->lines[curY].length;
                    }
                    break;
            }
            SetCaretPos(curX * charWidth + 10, curY * charHeight + 10);

            if (GetKeyState(VK_CONTROL) & 0x8000) { // Ctrl 키가 눌려있다면
                if (wParam == 'S') { // Ctrl + S
                    SaveLomoFile(L"my_code.c", doc);
                    MessageBoxW(hwnd, L"저장 완료!", L"LoMo", MB_OK);
                }
                else if (wParam == 'O') { // Ctrl + O
                    LoadLomoFile(hwnd, L"my_code.c", &doc);
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);

            // 1. Y좌표를 통해 몇 번째 줄인지 계산
            int clickedY = (mouseY - 10) / charHeight;
            if (clickedY < 0) clickedY = 0;
            if (clickedY >= doc->totalLines) clickedY = doc->totalLines - 1;

            // 2. X좌표를 통해 몇 번째 글자인지 계산
            int clickedX = (mouseX - 10 + (charWidth / 2)) / charWidth; // 반올림 효과
            if (clickedX < 0) clickedX = 0;
            if (clickedX > doc->lines[clickedY].length) clickedX = doc->lines[clickedY].length;

            // 3. 실제 좌표 업데이트
            curX = clickedX;
            curY = clickedY;
            SetCaretPos(curX * charWidth + 10, curY * charHeight + 10);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"LomoUIWindowClass";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    
    RegisterClass(&wc);
    
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Lomo UI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    BOOL USE_DARK_MODE = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &USE_DARK_MODE, sizeof(USE_DARK_MODE)); // DWMWA_USE_IMMERSIVE_DARK_MODE

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}