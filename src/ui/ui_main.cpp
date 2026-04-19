// src/ui/ui_main.cpp
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "editor.h"
#include <stdio.h>
#include <stdlib.h>

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Prototypes from ui_render.cpp
void DrawBehaviorOverlayImGui(ImDrawList* draw_list, ImVec2 pos, int value, float charHeight);
void DrawLomoConnectionImGui(ImDrawList* draw_list, ImVec2 p0, ImVec2 p3, float energy);
void RenderLineText(ImDrawList* draw_list, ImVec2 pos, const wchar_t* text, int length);

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "LoMo Cross-Platform UI (Phase C)", nullptr, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Initialize LoMo Editor Context
    EditorContext* ctx = (EditorContext*)malloc(sizeof(EditorContext));
    ctx->doc = CreateDocument();
    ctx->curX = 0;
    ctx->curY = 0;
    ctx->charHeight = 20;
    ctx->scrollY = 0;
    ctx->scrollX = 0;

    ImVec4 clear_color = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Pass characters and keys to our editor
        if (io.WantTextInput) {
            for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
                EditorCharInput(ctx, io.InputQueueCharacters[i]);
            }
        }
        
        // Basic key handling
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) EditorKeyDown(ctx, 0x101, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) EditorKeyDown(ctx, 0x102, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) EditorKeyDown(ctx, 0x103, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) EditorKeyDown(ctx, 0x104, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) EditorKeyDown(ctx, 0x105, io.KeyCtrl, io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) EditorKeyDown(ctx, 0x106, io.KeyCtrl, io.KeyShift);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Sidebar / Control
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(300, (float)io.DisplaySize.y));
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::Text("LoMo v2.0-cross");
        ImGui::Separator();
        if (ImGui::Button("Load Sample File")) {
            LoadFileContent(ctx->doc, "dummy_web.log");
        }
        ImGui::Text("Lines: %d", ctx->doc->lineCount);
        ImGui::Text("Cursor: %d, %d", ctx->curX, ctx->curY);
        ImGui::End();

        // 2. Editor Surface (Custom Drawing)
        ImGui::SetNextWindowPos(ImVec2(300, 0));
        ImGui::SetNextWindowSize(ImVec2((float)io.DisplaySize.x - 300, (float)io.DisplaySize.y));
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_HorizontalScrollbar);
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 base_pos = ImGui::GetCursorScreenPos();

        // Draw selection / cursor
        float cursor_x = base_pos.x + 10.0f + (float)ctx->curX * 8.0f;
        float cursor_y = base_pos.y + 10.0f + (float)ctx->curY * (float)ctx->charHeight;
        draw_list->AddRectFilled(ImVec2(cursor_x, cursor_y), ImVec2(cursor_x + 2, cursor_y + (float)ctx->charHeight), IM_COL32(0, 255, 0, 255));

        // Render each line
        for (int i = 0; i < ctx->doc->lineCount; i++) {
            Line* line = &ctx->doc->lines[i];
            ImVec2 line_pos = ImVec2(base_pos.x + 10.0f, base_pos.y + 10.0f + (float)i * (float)ctx->charHeight);
            
            RenderLineText(draw_list, line_pos, line->text, line->length);
            
            // Visual Overlays (from storage analytics)
            if (line->vState.targetEnergy >= 0) {
                DrawBehaviorOverlayImGui(draw_list, line_pos, (int)line->vState.targetEnergy, (float)ctx->charHeight);
            }
        }
        
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            EditorMouseClick(ctx, mouse_pos.x - base_pos.x, mouse_pos.y - base_pos.y);
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    DestroyDocument(ctx->doc);
    free(ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
