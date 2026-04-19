#include "imgui.h"
#include "types.h"
#include <string.h>
#include <math.h>

// Helper to convert energy to color (ImGui uses IM_COL32)
ImU32 GetEnergyColorImGui(int energy) {
    if (energy >= 80)      return IM_COL32(255, 50, 50, 255);   // Red
    else if (energy >= 50) return IM_COL32(255, 165, 0, 255);  // Orange
    else if (energy >= 0)  return IM_COL32(50, 200, 50, 255);   // Green
    return IM_COL32(100, 100, 255, 255); // Blue
}

void DrawBehaviorOverlayImGui(ImDrawList* draw_list, ImVec2 pos, int value, float charHeight) {
    if (value < 0) return;

    float barWidth = (float)value * 2.0f;
    float barHeight = charHeight - 4.0f;
    
    ImU32 color = GetEnergyColorImGui(value);
    
    ImVec2 p_min = ImVec2(pos.x + 600.0f, pos.y + 2.0f);
    ImVec2 p_max = ImVec2(p_min.x + barWidth, p_min.y + barHeight);
    
    draw_list->AddRectFilled(p_min, p_max, color);
}

void DrawLomoConnectionImGui(ImDrawList* draw_list, ImVec2 p0, ImVec2 p3, float energy) {
    float dist = p3.x - p0.x;
    float curvature = 0.5f;

    ImVec2 p1 = ImVec2(p0.x + dist * curvature, p0.y);
    ImVec2 p2 = ImVec2(p3.x - dist * curvature, p3.y);

    float thickness = (energy > 0.8f) ? 3.0f : 1.0f;
    ImU32 color = (energy > 0.8f) ? IM_COL32(0, 255, 255, 255) : IM_COL32(150, 150, 150, 255);

    draw_list->AddBezierCubic(p0, p1, p2, p3, color, thickness);
}

// Custom text rendering for our Line structure (wchar_t based)
void RenderLineText(ImDrawList* draw_list, ImVec2 pos, const wchar_t* text, int length) {
    // Note: ImGui handles UTF-8. For MVP Phase C, we might need a wchar_t to UTF-8 conversion
    // but for now, we'll cast or assume ASCII-compatible content if possible.
    // In a full implementation, we use a utility to convert to UTF-8.
    char buffer[1024];
    int i = 0;
    for(; i < length && i < 1023; i++) buffer[i] = (char)text[i];
    buffer[i] = '\0';
    
    draw_list->AddText(pos, IM_COL32_WHITE, buffer);
}
