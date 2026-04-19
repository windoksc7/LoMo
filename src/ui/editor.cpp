#include "types.h"
#include "editor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void InsertChar(Line* line, wchar_t ch, int pos) {
    if (!line || !line->text) return;

    if (line->length + 1 >= line->capacity) {
        int newSize = line->capacity * 2;
        wchar_t* newText = (wchar_t*)realloc(line->text, sizeof(wchar_t) * newSize);
        if (newText) {
            line->text = newText;
            line->capacity = newSize;
        }
    }

    if (pos < line->length) {
        memmove(&line->text[pos + 1], &line->text[pos], sizeof(wchar_t) * (line->length - pos));
    }

    line->text[pos] = ch;
    line->length++;
    line->text[line->length] = L'\0';
    line->isDirty = true;
}

void HandleEnter(EditorContext* ctx) {
    Document* doc = ctx->doc;
    if (doc->lineCount >= doc->maxLines) {
        int newMax = doc->maxLines * 2;
        Line* temp = (Line*)realloc(doc->lines, newMax * sizeof(Line));
        if (!temp) return;
        memset(&temp[doc->maxLines], 0, sizeof(Line) * (newMax - doc->maxLines));
        doc->lines = temp;
        doc->maxLines = newMax;
    }

    int shiftCount = doc->lineCount - (ctx->curY + 1);
    if (shiftCount > 0) {
        memmove(&doc->lines[ctx->curY + 2], &doc->lines[ctx->curY + 1], sizeof(Line) * shiftCount);
    }
    
    Line* newLine = &doc->lines[ctx->curY + 1];
    memset(newLine, 0, sizeof(Line));
    ResetLineState(newLine);
    doc->lineCount++;

    Line* currentLine = &doc->lines[ctx->curY];
    int moveLen = currentLine->length - ctx->curX;

    newLine->capacity = (moveLen > 32) ? moveLen + 32 : 64;
    newLine->text = (wchar_t*)malloc(newLine->capacity * sizeof(wchar_t));
    
    if (newLine->text) {
        if (moveLen > 0) {
            memcpy(newLine->text, &currentLine->text[ctx->curX], moveLen * sizeof(wchar_t));
        }
        newLine->length = moveLen;
        newLine->text[moveLen] = L'\0';
    }

    currentLine->length = ctx->curX;
    if (currentLine->text) currentLine->text[ctx->curX] = L'\0';

    ctx->curY++;
    ctx->curX = 0;

    for (int i = 0; i < doc->lineCount; i++) {
        doc->lines[i].isDirty = true;
    }
}

void HandleBackSpace(EditorContext* ctx) {
    if (ctx->curX > 0) {
        Line* line = &ctx->doc->lines[ctx->curY];
        if (ctx->curX < line->length) {
            memmove(&line->text[ctx->curX - 1], &line->text[ctx->curX], 
                    (line->length - ctx->curX + 1) * sizeof(wchar_t));
        }
        line->length--;
        line->text[line->length] = L'\0';
        ctx->curX--;
        line->isDirty = true;
    } else if (ctx->curY > 0) {
        Line* prevLine = &ctx->doc->lines[ctx->curY - 1];
        Line* curLine = &ctx->doc->lines[ctx->curY];
        int oldPrevLen = prevLine->length;

        if (curLine->length > 0) {
            if (prevLine->length + curLine->length >= prevLine->capacity) {
                int newSize = prevLine->length + curLine->length + 32;
                wchar_t* temp = (wchar_t*)realloc(prevLine->text, sizeof(wchar_t) * newSize);
                if (temp) {
                    prevLine->text = temp;
                    prevLine->capacity = newSize;
                }
            }
            memcpy(&prevLine->text[oldPrevLen], curLine->text, curLine->length * sizeof(wchar_t));
            prevLine->length += curLine->length;
            prevLine->text[prevLine->length] = L'\0';
            prevLine->isDirty = true;
        }

        if (curLine->text) free(curLine->text);

        int linesToShift = ctx->doc->lineCount - (ctx->curY + 1);
        if (linesToShift > 0) {
            memmove(&ctx->doc->lines[ctx->curY], &ctx->doc->lines[ctx->curY + 1], sizeof(Line) * linesToShift);
        }

        ctx->doc->lineCount--;
        ctx->curY--;
        ctx->curX = oldPrevLen;
        memset(&ctx->doc->lines[ctx->doc->lineCount], 0, sizeof(Line));
    }
}

void EditorKeyDown(EditorContext* ctx, int key, bool ctrl, bool shift) {
    if (!ctx || !ctx->doc) return;

    switch (key) {
        case 0x101: // Left
            if (ctx->curX > 0) ctx->curX--;
            else if (ctx->curY > 0) {
                ctx->curY--;
                ctx->curX = ctx->doc->lines[ctx->curY].length;
            } break;
        case 0x102: // Right
            if (ctx->curX < ctx->doc->lines[ctx->curY].length) ctx->curX++;
            else if (ctx->curY < ctx->doc->lineCount - 1) {
                ctx->curY++;
                ctx->curX = 0;
            } break;
        case 0x103: // Up
            if (ctx->curY > 0) {
                ctx->curY--;
                if (ctx->curX > ctx->doc->lines[ctx->curY].length) ctx->curX = ctx->doc->lines[ctx->curY].length;
            } break;
        case 0x104: // Down
            if (ctx->curY < ctx->doc->lineCount - 1) {
                ctx->curY++;
                if (ctx->curX > ctx->doc->lines[ctx->curY].length) ctx->curX = ctx->doc->lines[ctx->curY].length;
            } break;
        case 0x105: // Backspace
            HandleBackSpace(ctx); break;
        case 0x106: // Enter
            HandleEnter(ctx); break;
    }
}

void EditorCharInput(EditorContext* ctx, unsigned int c) {
    if (!ctx || !ctx->doc || ctx->curY >= ctx->doc->lineCount) return;
    if (c >= 32) {
        InsertChar(&ctx->doc->lines[ctx->curY], (wchar_t)c, ctx->curX);
        ctx->curX++;
    }
}

void EditorMouseClick(EditorContext* ctx, float x, float y) {
    if (!ctx || !ctx->doc || ctx->doc->lineCount <= 0) return;
    int h = (ctx->charHeight > 0) ? ctx->charHeight : 20;
    int clickedY = (int)(y / h);
    if (clickedY < 0) clickedY = 0;
    if (clickedY >= ctx->doc->lineCount) clickedY = ctx->doc->lineCount - 1;
    ctx->curY = clickedY;

    Line* curLine = &ctx->doc->lines[ctx->curY];
    int clickedX = (int)((x - 10.0f) / 8.0f);
    if (clickedX < 0) clickedX = 0;
    if (clickedX > curLine->length) clickedX = curLine->length;
    ctx->curX = clickedX;
}

// Stubs for Phase C MVP
void AnalyzeLineEnergy(Line* allLines, int currentIdx, int totalCount) { (void)allLines; (void)currentIdx; (void)totalCount; }
void UpdateVisuals(Document* doc) { (void)doc; }
