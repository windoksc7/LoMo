#include "types.h"
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void LoadFileContent(Document* doc, const char* filename) {
    if (!doc || !filename) return;

    DestroyDocument(doc); 
    doc->lineCount = 0;
    doc->maxLines = 100;
    doc->lines = (Line*)calloc(doc->maxLines, sizeof(Line));

    FILE* fp = fopen(filename, "rb");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(fp);
        AddLine(doc);
        return;
    }

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer) {
        size_t read = fread(buffer, 1, fileSize, fp);
        buffer[read] = '\0';

        char* lineStart = buffer;
        char* nextLine = NULL;

        while (lineStart < buffer + read) {
            nextLine = strchr(lineStart, '\n');
            int rawLen = nextLine ? (int)(nextLine - lineStart) : (int)(buffer + read - lineStart);
            
            int finalLen = rawLen;
            if (finalLen > 0 && lineStart[finalLen - 1] == '\r') finalLen--;

            if (doc->lineCount >= doc->maxLines) AddLine(doc);
            Line* line = &doc->lines[doc->lineCount - 1];

            // Platform-independent multi-byte to wide-char conversion
            char temp[2048]; // Buffer for conversion
            int copyLen = (finalLen < 2047) ? finalLen : 2047;
            memcpy(temp, lineStart, copyLen);
            temp[copyLen] = '\0';

            size_t wLen = mbstowcs(NULL, temp, 0);
            if (wLen == (size_t)-1) wLen = finalLen; // Fallback
            
            line->capacity = (int)wLen + 1;
            line->text = (wchar_t*)calloc(line->capacity, sizeof(wchar_t));
            mbstowcs(line->text, temp, wLen);
            line->length = (int)wLen;
            line->isDirty = true; 

            if (!nextLine) break;
            lineStart = nextLine + 1;
            AddLine(doc);
        }
        free(buffer);
    }

    fclose(fp);
}

Document* CreateDocument() {
    Document* doc = (Document*)malloc(sizeof(Document));
    if (!doc) return NULL;
    doc->lineCount = 0;
    doc->maxLines = 10; 
    doc->lines = (Line*)calloc(doc->maxLines, sizeof(Line));
    if (!doc->lines) {
        free(doc);
        return NULL;
    }
    AddLine(doc); 
    return doc;
}

void AddLine(Document* doc) {
    if (doc->lineCount >= doc->maxLines) {
        int newMax = doc->maxLines + 50;
        Line* temp = (Line*)realloc(doc->lines, sizeof(Line) * newMax);
        if (temp) {
            doc->lines = temp;
            memset(&doc->lines[doc->maxLines], 0, sizeof(Line) * (newMax - doc->maxLines));
            doc->maxLines = newMax;
        } else {
            return; 
        }
    }

    Line* newLine = &doc->lines[doc->lineCount];
    newLine->capacity = 128;
    newLine->length = 0;
    newLine->text = (wchar_t*)calloc(newLine->capacity, sizeof(wchar_t));
    if (newLine->text) {
        doc->lineCount++;
    }
}

void DestroyDocument(Document* doc) {
    if (!doc) return;
    for (int i = 0; i < doc->lineCount; i++) {
        if (doc->lines[i].text) free(doc->lines[i].text);
    }
    if (doc->lines) free(doc->lines);
    doc->lines = NULL;
    doc->lineCount = 0;
    doc->maxLines = 0;
}

void ResetLineState(Line* line) {
    if (!line) return;
    line->vState.targetEnergy = -1;
    line->targetLine = -1;
    line->isDirty = false;
    memset(line->unitName, 0, sizeof(line->unitName));
    memset(line->targetName, 0, sizeof(line->targetName));
}
