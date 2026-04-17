#pragma execution_character_set("utf-8")

#undef UNICODE
#undef _UNICODE

#include <windows.h>
#include <dwmapi.h>
#include <string.h>
#include <imm.h>
#include <commdlg.h>
#include <stdio.h>
#include "types.h"  // EditorContext, Document 정의
#include "editor.h" // OnCreate, OnChar, OnPaint 등 함수 선언

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"imm32.lib")

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 윈도우에 저장된 컨텍스트(데이터)를 가져옴
    EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
        case WM_CREATE:
            OnCreate(hwnd, (LPCREATESTRUCT)lParam); // 여기서 ctx 할당 및 초기화
            SetTimer(hwnd, 1, 33, NULL); // 약 30FPS로 심박수 설정
            return 0;
        // main.c 또는 WindowProc 내부
        case WM_SETFOCUS:
            // 커서(깜빡이는 막대)를 생성합니다. (너비 2, 높이는 폰트 높이)
            CreateCaret(hwnd, NULL, 2, ctx->charHeight);
            ShowCaret(hwnd);
            return 0;

        case WM_KILLFOCUS:
            HideCaret(hwnd);
            DestroyCaret();
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

            if (ctx) {
                HDC memDC;
                HBITMAP hbmMem, hOldBtn;
                RECT rect;

                // 더블 버퍼링 준비 (도화지 깔기)
                BeginDoubleBuffering(hdc, hwnd, &memDC, &hbmMem, &hOldBtn, &rect);
                // 실제 그리기 (우리가 만든 그 함수!)
                RenderAllElements(ctx, memDC);
                // 화면에 뿌리고 정리 (도화지 걷기)
                EndDoubleBuffering(hdc, memDC, hbmMem, hOldBtn, &rect);
            }
            EndPaint(hwnd, &ps);
        }
        return 0;

        case WM_CHAR:
        // ctx를 직접 넘기지 말고 hwnd를 넘겨서 editor.c의 OnChar와 형식을 맞춥니다.
            OnChar(hwnd, (wchar_t)wParam, (int)(short)LOWORD(lParam));
        return 0;

        case WM_KEYDOWN:
            if (ctx) {
                OnKeyDown(hwnd,
                    (UINT)wParam,            // vk (가상 키 코드)
                    TRUE,                    // fDown
                    (int)(short)LOWORD(lParam), // cRepeat (반복 횟수)
                    (UINT)HIWORD(lParam)     // flags
                );
                return 0; // 처리 완료
            }
            break; // ctx가 없으면 기본 처리로
        case WM_DESTROY:
            if (ctx) {
                DestroyDocument(ctx->doc);
                if (ctx->hFont) DeleteObject(ctx->hFont);
                free(ctx); // 할당한 컨텍스트 해제
            }
            PostQuitMessage(0);
            return 0;
        case WM_IME_COMPOSITION:
            if (ctx && (lParam & GCS_RESULTSTR)) {
                HIMC himc = ImmGetContext(hwnd);
                wchar_t wBuf[2] = {0};
                ImmGetCompositionStringW(himc, GCS_RESULTSTR, wBuf, sizeof(wBuf));
                
                // 전역 변수 대신 ctx 내부 멤버 사용
                InsertChar(&ctx->doc->lines[ctx->curY], wBuf[0], ctx->curX);
                ctx->curX++;
                
                UpdateCursorAndUI(ctx); // 함수 인자를 ctx로 단순화 추천
                ImmReleaseContext(hwnd, himc);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (ctx){
                OnLButtonDown(
                    hwnd, 
                    FALSE,                   // fDoubleClick
                    (int)(short)LOWORD(lParam), // x (마우스 X 좌표)
                    (int)(short)HIWORD(lParam), // y (마우스 Y 좌표)
                    (UINT)wParam             // keyFlags (Ctrl, Shift 상태 등)
                );
            }
            return 0;
        }
        case WM_TIMER:
            {
                EditorContext* ctx = (EditorContext*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                if (ctx) {
                    UpdateVisuals(ctx->doc); // 에너지 수치 갱신
                    InvalidateRect(hwnd, NULL, FALSE); // 화면 다시 그리기 (지우지 않고 덮어쓰기 권장)
                }
            }
            break;
        case WM_ERASEBKGND:
            // 배경을 시스템이 자동으로 지우지 못하게 0이 아닌 값을 반환합니다.
            return 1;
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
        0, CLASS_NAME, "Lomo UI", WS_OVERLAPPEDWINDOW,
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