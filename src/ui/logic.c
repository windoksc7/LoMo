#include "types.h"
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// editor.c 또는 별도 logic.c
void UpdateVisuals(Document* doc) {
    for (int i = 0; i < doc->lineCount; i++) {
        VisualState* vs = &doc->lines[i].vState;
        
        // [Zeno's Paradox Interpolation] 
        // 목표치와 현재치의 차이를 일정 비율(0.1f)씩 줄여나갑니다.
        float gap = vs->targetEnergy - vs->currentEnergy;
        
        if (fabsf(gap) > 0.01f) {
            vs->currentEnergy += gap * 0.1f; // 이 수치가 낮을수록 묵직하게 변합니다.
        } else {
            vs->currentEnergy = vs->targetEnergy;
        }
    }
}