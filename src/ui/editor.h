#ifndef LOMO_EDITOR_H
#define LOMO_EDITOR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- document.c related ---
Document* CreateDocument();
void AddLine(Document* doc);
void DestroyDocument(Document* doc);
void LoadFileContent(Document* doc, const char* filename);

// --- editor.c related ---
void InsertChar(Line* line, wchar_t ch, int pos);
void HandleBackSpace(EditorContext* ctx);
void HandleEnter(EditorContext* ctx);
void ResetLineState(Line* line);

// Generic Input Handlers (for ImGui to call)
void EditorKeyDown(EditorContext* ctx, int key, bool ctrl, bool shift);
void EditorCharInput(EditorContext* ctx, unsigned int c);
void EditorMouseClick(EditorContext* ctx, float x, float y);

// --- analysis/visuals ---
void AnalyzeLineEnergy(Line* allLines, int currentIdx, int totalCount);
void UpdateVisuals(Document* doc);

#ifdef __cplusplus
}
#endif

#endif // LOMO_EDITOR_H
