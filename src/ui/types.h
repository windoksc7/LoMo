#ifndef LOMO_TYPES_H
#define LOMO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

typedef struct {
    float currentEnergy;  // 현재 화면에 보이는 에너지 (보간용)
    float targetEnergy;   // 파서가 계산한 목표 에너지
    float waveOffset;     // (추후) 광통신 입자 이동용 오프셋
} VisualState;

// 1. 개별 줄 데이터 구조
typedef struct {
    wchar_t* text;
    int length;
    int capacity;
     
    int targetLine;      
    VisualState vState;   // 구조체 안에 포함 (접근이 편함)
    int resolvedTargetIdx;
    bool isDirty;     
    char unitName[64];   
    char targetName[64]; 
} Line;

// 2. 전체 문서 데이터 구조
typedef struct {
    Line* lines;      // Line 구조체 배열
    int lineCount;   // 전체 줄 수
    int maxLines;     // 할당 가능한 최대 줄 수
} Document;

// 3. 에디터 통합 컨텍스트 (리팩토링의 핵심)
typedef struct {
    Document* doc;    // 문서 데이터 포인터
    int curX;         // 커서 X 인덱스
    int curY;         // 커서 Y 인덱스
    
    // 렌더링 및 UI 관련 상태
    void* os_handle;  // Replacement for HWND
    void* font_handle; // Replacement for HFONT
    int charWidth;    // 폰트 너비 (고정폭 대비용)
    int charHeight;   // 폰트 높이
    
    int scrollY;      
    int scrollX;
} EditorContext;

typedef struct {
    int lineNum;
    int value;
} BehaviorData;

typedef struct {
    float x, y;
} Vec2;

typedef struct {
    Vec2 pos;
    float energy; // 0.0 ~ 1.0 (신경계 역치용)
} Unit;

#endif // LOMO_TYPES_H
