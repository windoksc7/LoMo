#include "types.h"
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>

// 기존의 텍스트 출력 루프 안에서 호출된다고 가정
void DrawBehaviorOverlay(HDC hdc, EditorContext* ctx, int lineY, int value) {
    if (!ctx || value < 0) return;

    int barWidth = value * 2; // 0~200px 사이로 가변
    int barHeight = ctx->charHeight - 4;

    SetBkMode(hdc, TRANSPARENT);

    COLORREF barColor = GetEnergyColor(value);

    HBRUSH hBrush = CreateSolidBrush(barColor);
    HPEN hPen = CreatePen(PS_NULL, 0, 0); // 테두리 없음

    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    
    SelectObject(hdc, hBrush);
    SelectObject(hdc, hPen);

    // 코드 우측(600px 지점)에 사각형 막대 그리기
    int left = 600;
    int top = lineY + 2;
    int right = 600 + barWidth;
    int bottom = top + barHeight;

    //SetPixel(hdc, 600, lineY + 5, RGB(255, 0, 0));
    // 좌표가 정상적인지 디버깅 (오른쪽 좌표가 왼쪽보다 커야 함)
    Rectangle(hdc, left, top, right, bottom);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

BehaviorData ParseBehaviorC(const char* line, int currentLineNum) {
    BehaviorData data = { currentLineNum, -1 };
    
    // 1. "energy" 키워드 탐색
    const char* pos = strstr(line, "energy");
    
    if (pos != NULL) {
        // 2. "energy" 단어 바로 뒤부터 탐색 시작
        char* p = (char*)pos + 6; 
        
        // 3. '=' 또는 '공백'이 나오면 계속 건너뜁니다 (유연함의 핵심)
        while (*p && !(*p >= '0' && *p <= '9') && *p != '-') {
            p++;
        }
        if (*p) {
            data.value = atoi(p); // 이제 "energy=80", "energy : 80" 모두 대응 가능
        }
    }
    
    return data;
}

// render.c 내 가상 로직
void DrawConnectionLine(HDC hdc, int startY, int endY) {
    // startY(현재 줄)에서 endY(대상 줄)까지 
    // 옆으로 살짝 삐져나온 곡선(Bezier)을 그리면 "신경망" 바이브가 확 살아납니다.
    MoveToEx(hdc, 200, startY, NULL);
    LineTo(hdc, 200, endY); // 일단 직선으로 테스트!
}

// render.c
void DrawConnectionHarness(HDC hdc, EditorContext* ctx, int currentLineIdx, int yPos, int fontHeight) {
    Line* line = &ctx->doc->lines[currentLineIdx];
    if (line->targetLine == -1) return;

    int targetY = -1;
    int targetEnergy = -1;

    // 1. 대상 탐색 (숫자 또는 이름)
    if (line->targetLine >= 0 && line->targetLine < ctx->doc->lineCount) {
        targetY = (line->targetLine * fontHeight) + 10;
        targetEnergy = ctx->doc->lines[line->targetLine].vState.targetEnergy;
    } 
    else if (line->targetLine == -2 && line->targetName[0] != '\0') {
        for (int j = 0; j < ctx->doc->lineCount; j++) {
            if (ctx->doc->lines[j].unitName[0] != '\0' && 
                strcmp(line->targetName, ctx->doc->lines[j].unitName) == 0) {
                targetY = (j * fontHeight) + 10;
                targetEnergy = ctx->doc->lines[j].vState.targetEnergy;
                break;
            }
        }
    }

    // 2. 그리기
    if (targetY != -1) {
        // [분리된 함수 호출] 타겟의 에너지에 따라 선 색깔을 가져옵니다.
        COLORREF syncColor = GetEnergyColor(targetEnergy);
        RenderHarnessLine(hdc, 210, yPos + (fontHeight / 2), 210, targetY + (fontHeight / 2), syncColor);
    }
}

// render.c 상단 혹은 적절한 위치에 추가
COLORREF GetEnergyColor(int energy) {
    if (energy >= 80)      return RGB(255, 50, 50);   // 과부하 (Red)
    else if (energy >= 50) return RGB(255, 165, 0);  // 활성 (Orange)
    else if (energy >= 0)  return RGB(50, 200, 50);   // 안정 (Green)
    
    return RGB(100, 100, 255); // 기본/알 수 없음 (Blue)
}

// [그리기 전용 함수] 실제 GDI 호출만 담당합니다.
void RenderHarnessLine(HDC hdc, int xStart, int yStart, int xEnd, int yEnd, COLORREF color) {
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // 1. ㄷ자 배선 그리기
    MoveToEx(hdc, xStart, yStart, NULL);
    LineTo(hdc, xStart - 30, yStart);
    LineTo(hdc, xStart - 30, yEnd);
    LineTo(hdc, xStart, yEnd);

    // 2. 화살표 머리 추가 (도착 지점 xStart, yEnd)
    // 에너지가 흐르는 방향을 찍어줍니다.
    MoveToEx(hdc, xStart, yEnd, NULL);
    LineTo(hdc, xStart - 5, yEnd - 3);
    MoveToEx(hdc, xStart, yEnd, NULL);
    LineTo(hdc, xStart - 5, yEnd + 3);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

// 2. 3차 베지어 곡선 계산 함수 (De Casteljau 알고리즘의 최적화 버전)
// ksc님의 1.3MB를 위해 math.h의 pow 함수 대신 직접 곱셈을 사용합니다.
Vec2 CalculateBezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float invT = 1.0f - t;
    float b0 = invT * invT * invT;
    float b1 = 3.0f * invT * invT * t;
    float b2 = 3.0f * invT * t * t;
    float b3 = t * t * t;

    Vec2 res;
    res.x = b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x;
    res.y = b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y;
    return res;
}

// 3. 에너지 흐름이 담긴 곡선 그리기 함수
void DrawLomoConnection(HDC hdc, Unit* start, Unit* end) {
    // [광통신 원리] 거리에 따른 유연한 제어점(Control Points) 설정
    float dist = end->pos.x - start->pos.x;
    float curvature = 0.5f; // ksc님이 조절할 곡률 상수

    Vec2 p0 = start->pos;
    Vec2 p3 = end->pos;
    Vec2 p1 = { p0.x + dist * curvature, p0.y };
    Vec2 p2 = { p3.x - dist * curvature, p3.y };

    // [신경계 역치 원리] 에너지가 높으면 선이 굵어지고 색이 변함
    int penWidth = (start->energy > 0.8f) ? 3 : 1; 
    COLORREF lineColor = (start->energy > 0.8f) ? RGB(0, 255, 255) : RGB(150, 150, 150);

    HPEN hPen = CreatePen(PS_SOLID, penWidth, lineColor);
    SelectObject(hdc, hPen);

    // 곡선 렌더링 (해상도 조절 가능)
    MoveToEx(hdc, (int)p0.x, (int)p0.y, NULL);
    
    int segments = 20; // 1.3MB를 위해 적절한 세그먼트 수 유지
    for (int i = 1; i <= segments; i++) {
        float t = (float)i / (float)segments;
        Vec2 pos = CalculateBezier(p0, p1, p2, p3, t);
        LineTo(hdc, (int)pos.x, (int)pos.y);
    }

    DeleteObject(hPen);
}

void RenderAllElements(EditorContext* ctx, HDC hdc) {
    if (!ctx || !ctx->doc) return;

    // 1. 렌더링 설정 (기존과 동일)
    SelectObject(hdc, ctx->hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    int fontHeight = (ctx->charHeight > 0) ? ctx->charHeight : 20;

    for (int i = 0; i < ctx->doc->lineCount; i++) {
        Line* line = &ctx->doc->lines[i];
        int yPos = (i * fontHeight) + 10;

        // [최적화] 이미 분석된 데이터라면 다시 분석하지 않음
        if (line->isDirty) {
            AnalyzeLineEnergy(ctx->doc->lines, i, ctx->doc->lineCount); 
        }

        // 1. 텍스트 출력
        TextOutW(hdc, 10, yPos, line->text, line->length);

        // 2. 행동 오버레이 (막대기 등)
        if (line->vState.targetEnergy != -1) {
            DrawBehaviorOverlay(hdc, ctx, yPos, line->vState.targetEnergy);
        }

        // 3. 신경망 곡선 그리기
        if (line->resolvedTargetIdx >= 0 && line->resolvedTargetIdx < ctx->doc->lineCount) {
            // [마법] targetEnergy 대신 실시간 변하는 currentEnergy를 사용!
            Unit startPoint = { { 200.0f, (float)yPos }, line->vState.currentEnergy }; 
            Unit endPoint = { { 10.0f, (float)(line->resolvedTargetIdx * fontHeight + 10) }, 0.5f };

            DrawLomoConnection(hdc, &startPoint, &endPoint);
        }
    }
}

// editor.c 또는 별도 render.c
void BeginDoubleBuffering(HDC hdc, HWND hwnd, HDC* memDC, HBITMAP* hbmMem, HBITMAP* hOldBtn, RECT* rect) {
    GetClientRect(hwnd, rect);
    
    // 1. 실제 화면(hdc)과 호환되는 가상 화면(memDC) 생성
    *memDC = CreateCompatibleDC(hdc);
    
    // 2. 가상 화면에 꽂을 비트맵(도화지) 생성
    *hbmMem = CreateCompatibleBitmap(hdc, rect->right, rect->bottom);
    
    // 3. 가상 화면에 도화지를 끼우고, 이전 상태를 저장
    *hOldBtn = (HBITMAP)SelectObject(*memDC, *hbmMem);

    // 4. 배경을 깨끗하게 칠함 (깜빡임 방지의 핵심)
    FillRect(*memDC, rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
}

void EndDoubleBuffering(HDC hdc, HDC memDC, HBITMAP hbmMem, HBITMAP hOldBtn, RECT* rect) {
    // 1. 완성된 가상 화면을 실제 화면으로 복사
    BitBlt(hdc, 0, 0, rect->right, rect->bottom, memDC, 0, 0, SRCCOPY);

    // 2. 사용한 리소스 정리 (메모리 누수 방지)
    SelectObject(memDC, hOldBtn);
    DeleteObject(hbmMem);
    DeleteDC(memDC);
}