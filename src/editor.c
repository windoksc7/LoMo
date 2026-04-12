#pragma comment(lib, "legacy_stdio_definitions.lib")
#include "types.h" // 구조체 정의 포함 가정
#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

void InsertChar(Line* line, wchar_t ch, int pos) {
    if (!line || !line->text) return;

    // 1. 공간이 부족하면 확장 (2배 확장 전략)
    if (line->length + 1 >= line->capacity) {
        int newSize = line->capacity * 2;
        wchar_t* newText = (wchar_t*)realloc(line->text, sizeof(wchar_t) * newSize);
        if (newText) {
            line->text = newText;
            line->capacity = newSize;
        }
    }

    // 2. 중간 삽입을 위한 memmove (현재는 단순 추가 테스트)
    // pos 위치 뒤의 글자들을 한 칸씩 뒤로 밉니다.
    memmove(&line->text[pos + 1], &line->text[pos], sizeof(wchar_t) * (line->length - pos));

    // 3. 글자 삽입 및 길이 증가 (이게 핵심!)
    line->text[pos] = ch;
    line->length++;
    line->text[line->length] = L'\0'; // 널 종료 문자 처리
    line->isDirty = TRUE; // [추가] 분석 엔진을 깨우는 신호!
}

// editor.c 내 수정 권장 로직
void HandleEnter(EditorContext* ctx) {
    Document* doc = ctx->doc;

    // 1. 배열 확장
    if (doc->lineCount >= doc->maxLines) {
        int newMax = doc->maxLines * 2;
        Line* temp = (Line*)realloc(doc->lines, newMax * sizeof(Line));
        if (!temp) return;
        // 새로 생긴 빈 공간을 0으로 밀어줘야 나중에 쓰레기 값을 참조 안 함
        memset(&temp[doc->maxLines], 0, sizeof(Line) * (newMax - doc->maxLines));
        doc->lines = temp;
        doc->maxLines = newMax;
    }

    // 2. 줄 밀기 (현재 줄 다음부터 끝까지)
    int shiftCount = doc->lineCount - (ctx->curY + 1);
    if (shiftCount > 0) {
        memmove(&doc->lines[ctx->curY + 2], &doc->lines[ctx->curY + 1], sizeof(Line) * shiftCount);
    }
    
    // 3. 새 줄(newLine) 초기화 및 lineCount 증가
    Line* newLine = &doc->lines[ctx->curY + 1];
    memset(newLine, 0, sizeof(Line)); // 0으로 기본 밀기
    ResetLineState(newLine);         // [추가] 에너지 및 연결 상태 명시적 초기화
    doc->lineCount++;

    // 4. 텍스트 분할 로직
    Line* currentLine = &doc->lines[ctx->curY];
    int moveLen = currentLine->length - ctx->curX;

    newLine->capacity = (moveLen > 32) ? moveLen + 32 : 64;
    newLine->text = (wchar_t*)malloc(newLine->capacity * sizeof(wchar_t));
    
    if (newLine->text) {
        if (moveLen > 0) {
            memcpy(newLine->text, &currentLine->text[ctx->curX], moveLen * sizeof(wchar_t));
        }
        newLine->length = moveLen;
        newLine->text[moveLen] = L'\0';
    }

    // 5. 현재 줄 자르기
    currentLine->length = ctx->curX;
    if (currentLine->text) currentLine->text[ctx->curX] = L'\0';

    // 6. 커서 이동
    ctx->curY++;
    ctx->curX = 0;

    for (int i = 0; i < doc->lineCount; i++) {
        doc->lines[i].isDirty = TRUE;
    }
    // 7. [핵심] 물리적인 커서 위치를 새 줄의 0번 위치로 옮깁니다.
    UpdateCursorAndUI(ctx);
}

void HandleBackSpace(EditorContext* ctx) {
    if (ctx->curX > 0) {
        Line* line = &ctx->doc->lines[ctx->curY];
        if (ctx->curX < line->length) {
            memmove(&line->text[ctx->curX - 1], &line->text[ctx->curX], 
                    (line->length - ctx->curX + 1) * sizeof(wchar_t));
        }
        line->length--;
        line->text[line->length] = L'\0';
        ctx->curX--;
        line->isDirty = TRUE;
    }else if (ctx->curY > 0) {
        Line* prevLine = &ctx->doc->lines[ctx->curY - 1];
        Line* curLine = &ctx->doc->lines[ctx->curY];
        
        int oldPrevLen = prevLine->length;

        // 1. 현재 줄에 내용이 있을 때만 합치기 수행
        if (curLine->length > 0) {
            // 윗줄 공간 확보
            if (prevLine->length + curLine->length >= prevLine->capacity) {
                int newSize = prevLine->length + curLine->length + 32;
                wchar_t* temp = (wchar_t*)realloc(prevLine->text, sizeof(wchar_t) * newSize);
                if (temp) {
                    prevLine->text = temp;
                    prevLine->capacity = newSize;
                }
            }
            // 내용 복사
            memcpy(&prevLine->text[oldPrevLen], curLine->text, curLine->length * sizeof(wchar_t));
            prevLine->length += curLine->length;
            prevLine->text[prevLine->length] = L'\0';
            prevLine->isDirty = TRUE;
        }

        // 2. 현재 줄(빈 줄 포함)의 자원 해제
        if (curLine->text) {
            free(curLine->text);
            curLine->text = NULL; // Dangling Pointer 방지
        }

        // 3. [핵심] 줄 배열 밀기 (라인 3을 라인 2 자리로)
        // 현재 줄 다음부터 끝까지의 줄 개수를 계산
        int linesToShift = ctx->doc->lineCount - (ctx->curY + 1);
        if (linesToShift > 0) {
            // memmove를 사용하여 라인 3(idx: curY+1)부터의 정보를 라인 2(idx: curY)로 덮어씌움
            memmove(&ctx->doc->lines[ctx->curY], 
                    &ctx->doc->lines[ctx->curY + 1], 
                    sizeof(Line) * linesToShift);
        }

        // 4. 문서 전체 줄 수 감소 및 커서 이동
        ctx->doc->lineCount--;
        ctx->curY--;
        ctx->curX = oldPrevLen;
        
        // 5. 마지막 줄이 지워졌을 경우를 대비해 배열 끝부분 초기화 (선택 사항)
        memset(&ctx->doc->lines[ctx->doc->lineCount], 0, sizeof(Line));
    }
    // 커서와 UI 동기화
    UpdateCursorAndUI(ctx);
}

void UpdateCursorAndUI(EditorContext* ctx) {
    if (!ctx || !ctx->hwnd || !ctx->doc) return;

    SIZE size;
    HDC hdc = GetDC(ctx->hwnd);
    HFONT hOldFont = (HFONT)SelectObject(hdc, ctx->hFont);
    
    // 현재 줄의 텍스트 너비 계산
    Line* curLine = &ctx->doc->lines[ctx->curY];
    GetTextExtentPoint32W(hdc, curLine->text, ctx->curX, &size);
    
    SelectObject(hdc, hOldFont); // 기존 폰트 복구 (GDI 리소스 관리)
    ReleaseDC(ctx->hwnd, hdc);

    // 좌표 오프셋 적용
    SetCaretPos(size.cx + 10, ctx->curY * ctx->charHeight + 10);
    
    // 화면 전체 대신 현재 줄 근처만 갱신하면 더 빠르지만, 일단 전체 갱신 유지
    InvalidateRect(ctx->hwnd, NULL, TRUE);
}

LRESULT OnCreate(HWND hwnd, LPCREATESTRUCT lpcs) {
    // 1. EditorContext 메모리 할당
    EditorContext* ctx = (EditorContext*)malloc(sizeof(EditorContext));
    if (ctx == NULL) return -1;

    // 2. 데이터 구조 초기화 (document.c의 함수 사용)
    ctx->doc = CreateDocument(); // Document를 생성하는 함수가 있다고 가정
    ctx->curX = 0;
    ctx->curY = 0;
    ctx->hwnd = hwnd;
    ctx->scrollY = 0;
    
    // 폰트 및 렌더링 설정
    ctx->hFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    
    HDC hdc = GetDC(hwnd);
    SelectObject(hdc, ctx->hFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    ctx->charHeight = tm.tmHeight; // 줄 높이 저장
    ReleaseDC(hwnd, hdc);
    // 3. 윈도우 사용자 데이터 영역에 Context 포인터 저장 (매우 중요!)
    // 이 작업을 해야 WindowProc에서 다시 꺼내 쓸 수 있습니다.
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

    return 0;
}

// 1. 키보드 특수 키 처리 (화살표, Home, End 등)
void OnKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags) {
    EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!ctx) return;

    BOOL isCtrlPressed = (GetKeyState(VK_CONTROL) & 0x8000);
    
    switch (vk) {
        case VK_LEFT:  
            if (ctx->curX > 0) {
                ctx->curX--;
            } else if (ctx->curY > 0) {
                // 줄의 맨 앞이면 윗줄 끝으로 이동
                ctx->curY--;
                ctx->curX = ctx->doc->lines[ctx->curY].length;
            } break;
        case VK_RIGHT: 
            if (ctx->curX < ctx->doc->lines[ctx->curY].length) {
                ctx->curX++;
            } else if (ctx->curY < ctx->doc->lineCount - 1) {
                // 줄의 맨 끝이면 아랫줄 처음으로 이동
                ctx->curY++;
                ctx->curX = 0;
            }
            break;
        case VK_DOWN:  
            if (ctx->curY < ctx->doc->lineCount - 1) {
                ctx->curY++;
                // 아랫줄이 현재 줄보다 짧으면 아랫줄 끝으로 커서 보정
                if (ctx->curX > ctx->doc->lines[ctx->curY].length) {
                    ctx->curX = ctx->doc->lines[ctx->curY].length;
                }
            }
            break;
        case VK_UP:    
            if (ctx->curY > 0) {
                ctx->curY--;
                // 윗줄이 현재 줄보다 짧으면 윗줄 끝으로 커서 보정
                if (ctx->curX > ctx->doc->lines[ctx->curY].length) {
                    ctx->curX = ctx->doc->lines[ctx->curY].length;
                }
            }
            break;
        case VK_BACK:  HandleBackSpace(ctx); break; // 구현해두신 로직 연결
    }

    if (isCtrlPressed && vk == 'O') {
        OPENFILENAME ofn;       // 공통 대화상자 구조체
        char szFile[260] = { 0 }; // 파일 경로를 담을 버퍼

        // 구조체 초기화
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "C/C++ Files\0*.c;*.cpp;*.h\0Text Files\0*.txt\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        // 3. 파일 열기 창 띄우기
        if (GetOpenFileNameA(&ofn) == TRUE) {
            // 사용자가 파일을 선택했다면, 분석 함수 호출!
            OpenAndAnalyzeFile(hwnd, ofn.lpstrFile);
        }
    }
    UpdateCursorAndUI(ctx);
}

// 2. 마우스 클릭 처리 (커서 위치 이동)
// editor.c 내의 OnLButtonDown 수정
void OnLButtonDown(HWND hwnd, BOOL fDoubleClick, int x, int y, UINT keyFlags) {
    EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    // [방어 1] 데이터 구조가 없으면 즉시 종료
    if (!ctx || !ctx->doc || ctx->doc->lineCount <= 0) return;

    // [방어 2] 0으로 나누기 에러 방지 (최소값 1 확보)
    int h = (ctx->charHeight > 0) ? ctx->charHeight : 18; 
    
    // [방어 3] Y축 범위 제한: 클릭한 곳이 줄 수를 넘어가면 마지막 줄로 고정
    int clickedY = (y + ctx->scrollY) / h;
    if (clickedY < 0) clickedY = 0;
    if (clickedY >= ctx->doc->lineCount) clickedY = ctx->doc->lineCount - 1;
    ctx->curY = clickedY;

    // [방어 4] X축 범위 제한: 클릭한 곳이 줄 길이보다 멀면 줄 끝으로 고정
    Line* curLine = &ctx->doc->lines[ctx->curY];
    if (curLine && curLine->text) {
        // 시작 여백(10px)을 고려한 계산
        int clickedX = (x < 10) ? 0 : (x - 10) / 8; 
        if (clickedX < 0) clickedX = 0;
        if (clickedX > curLine->length) clickedX = curLine->length;
        ctx->curX = clickedX;
    } else {
        ctx->curX = 0;
    }

    SetFocus(hwnd); 
    UpdateCursorAndUI(ctx);
}

// 3. 문자 입력 처리 (일반 텍스트, 엔터 등)
void OnChar(HWND hwnd, TCHAR ch, int cRepeat) {
    EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    // [수정] ctx가 없거나 curY가 범위를 벗어나면 즉시 리턴하여 Crash 방지
    if (!ctx || !ctx->doc || ctx->curY < 0 || ctx->curY >= ctx->doc->lineCount) return;

    if (ch == L'\r') { 
        HandleEnter(ctx); 
    } else if (ctx->doc->lineCount == 0) {
        AddLine(ctx->doc);
    } else if (ch >= 32) { 
        Line* currentLine = &(ctx->doc->lines[ctx->curY]);
        if (currentLine && currentLine->text) {
            InsertChar(currentLine, (wchar_t)ch, ctx->curX);
            ctx->curX++;
            
        }
    }
    UpdateCursorAndUI(ctx);
}

void OnPaint(EditorContext* ctx, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // 1. 메모리 DC 생성 (가상 도화지)
    HDC memDC = CreateCompatibleDC(hdc);
    RECT rect;
    GetClientRect(hwnd, &rect);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    HBITMAP hOldBtn = (HBITMAP)SelectObject(memDC, hbmMem);

    // 2. 가상 도화지에 배경색 칠하기 (흰색)
    FillRect(memDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

    // 3. 기존의 모든 그리기 로직 (곡선, 텍스트 등)을 memDC에 수행
    // (기존 OnPaint의 내용을 hdc 대신 memDC로 호출하도록 변경)
    RenderAllElements(ctx, memDC); 

    // 4. 완성된 그림을 실제 화면(hdc)으로 한 번에 전송 (BitBlt)
    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

    // 5. 리소스 해제 (GDI 누수 방지 - 매우 중요!)
    SelectObject(memDC, hOldBtn);
    DeleteObject(hbmMem);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}