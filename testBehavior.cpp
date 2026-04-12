// test_behavior.cpp
#include <iostream>
#include <windows.h>

int main() {
    int energy = 0;
    for(int i = 0; i < 1000; i++) {
        energy = i % 50; // 0~50 サイクル
        
        // LoMoがこの視点の 'energy' を追跡して点をつけると仮定
        Sleep(100); 
    }
    return 0;
}