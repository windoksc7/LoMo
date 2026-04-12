// parser.c
#include "types.h"
#include "editor.h"
#include <windows.h>
#include <string.h>
#include <stdlib.h>

void AnalyzeLineEnergy(Line* allLines, int currentIdx, int totalCount) {
    Line* line = &allLines[currentIdx];
    
    // 1. 초기화 및 안전장치
    if (!line->text || line->length == 0) {
        ResetLineState(line);
        return;
    }

    // 2. ANSI 변환
    char tempAnsi[1024] = {0};
    WideCharToMultiByte(CP_ACP, 0, line->text, -1, tempAnsi, sizeof(tempAnsi), NULL, NULL);

    // 3. 에너지 수치 파싱
    BehaviorData bData = ParseBehaviorC(tempAnsi, currentIdx); 
    // parser.c의 AnalyzeLineEnergy 함수 내부
    // 기존: line->targetEnergy = bData.value;
    line->vState.targetEnergy = (float)bData.value; // [수정] vState를 거쳐서 저장

    // --- 여기서부터 하네스 재배선 ---

    // 4. 내 이름 정의 ("unit MyName")
    memset(line->unitName, 0, sizeof(line->unitName));
    char* pUnit = strstr(tempAnsi, "unit ");
    if (pUnit) {
        // "unit " (5글자) 뒤부터 토큰을 추출해서 line->unitName에 바로 저장!
        ExtractToken(pUnit + 5, line->unitName, 64);
    }

    // 5. 대상 연결 ("-> Target") - pTargetPos 정의 및 파싱
    line->targetLine = -1; 
    memset(line->targetName, 0, sizeof(line->targetName));
    
    // [중요] 여기서 pTargetPos를 선언합니다!
    char* pTargetPos = strstr(tempAnsi, "->"); 
    // AnalyzeLineEnergy 내부 수정
    if (pTargetPos) {
        char targetToken[64] = { 0 };
        // "->" 뒤의 단어를 토큰으로 추출 (공백은 함수가 알아서 처리)
        int len = ExtractToken(pTargetPos + 2, targetToken, 64);
        
        if (len > 0) {
            if (targetToken[0] >= '0' && targetToken[0] <= '9') {
                // 1. 숫자 방식
                line->targetLine = atoi(targetToken);
            } else {
                // 2. 이름 방식
                strncpy(line->targetName, targetToken, 63);
                line->targetLine = -2;
            }
        } else {
            line->targetLine = -1; // 토큰이 없으면 연결 해제
        }
    }

    // parser.c의 AnalyzeLineEnergy 함수 마지막 부분에 추가 권장
    if (line->targetLine == -2) { 
        line->resolvedTargetIdx = -1; // 초기화
        for (int j = 0; j < totalCount; j++) { // [수정] 인자로 받은 totalCount 사용
            if (strcmp(allLines[j].unitName, line->targetName) == 0) {
                line->resolvedTargetIdx = j; 
                break;
            }
        }
    } else {
        line->resolvedTargetIdx = line->targetLine; // 숫자 방식인 경우 그대로 저장
    }

    line->isDirty = FALSE;
}

// [추출 함수] 시작 포인터부터 공백/줄바꿈 전까지의 단어를 안전하게 복사합니다.
int ExtractToken(const char* src, char* dest, int maxLen) {
    // 1. 초기 공백 건너뛰기
    while (*src == ' ') src++;
    
    int k = 0;
    // 2. 유효한 문자만 복사 (공백, 줄바꿈, 널 문자 전까지)
    while (src[k] && src[k] != ' ' && src[k] != '\r' && src[k] != '\n' && k < maxLen - 1) {
        dest[k] = src[k];
        k++;
    }
    dest[k] = '\0'; // 널 종료
    return k; // 추출된 길이를 반환
}