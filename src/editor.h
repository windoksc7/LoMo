#ifndef LOMO_EDITOR_H
#define LOMO_EDITOR_H

#include "types.h"

// --- document.c 관련 함수 ---
Document* CreateDocument();
// 새로운 줄을 맨 뒤에 추가 (초기화 전용)
void AddLine(Document* doc);
// 문서 전체 메모리 해제
void DestroyDocument(Document* doc);


// --- editor.c 관련 함수 ---
// 특정 위치(pos)에 문자(ch) 삽입 (중간 삽입 대응)
void InsertChar(Line* line, wchar_t ch, int pos);

// 백스페이스 처리 (줄 합치기 로직 포함)
void HandleBackSpace(EditorContext* ctx);

// 엔터 처리 (줄 나누기 로직 포함)
void HandleEnter(EditorContext* ctx);

// 화면 갱신 및 커서 위치 재계산
void UpdateCursorAndUI(EditorContext* ctx);


// --- 추가될 수 있는 로직 (OnCreate, OnPaint 등) ---
// WindowProc에서 호출할 메시지별 핸들러
LRESULT OnCreate(HWND hwnd, LPCREATESTRUCT lpcs);
void OnPaint(EditorContext* ctx, HWND hwnd);
void OnKeyDown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags);
void OnLButtonDown(HWND hwnd, BOOL fDoubleClick, int x, int y, UINT keyFlags);

void DrawBehaviorOverlay(HDC hdc, EditorContext* ctx, int lineY, int value);
BehaviorData ParseBehaviorC(const char* line, int currentLineNum);
void OpenAndAnalyzeFile(HWND hwnd, const char* filename);
void LoadFileContent(Document* doc, const char* filename);

void AnalyzeLineEnergy(Line* allLines, int currentIdx, int totalCount);
void DrawConnectionHarness(HDC hdc, EditorContext* ctx, int currentLineIdx, int yPos, int fontHeight);
void ResetLineState(Line* line);
int ExtractToken(const char* src, char* dest, int maxLen);
COLORREF GetEnergyColor(int energy);
void RenderHarnessLine(HDC hdc, int xStart, int yStart, int xEnd, int yEnd, COLORREF color);

void UpdateVisuals(Document* doc);
void RenderAllElements(ctx, memDC);

void BeginDoubleBuffering(HDC hdc, HWND hwnd, HDC* memDC, HBITMAP* hbmMem, HBITMAP* hOldBtn, RECT* rect);
void EndDoubleBuffering(HDC hdc, HDC memDC, HBITMAP hbmMem, HBITMAP hOldBtn, RECT* rect);
#endif // LOMO_EDITOR_H