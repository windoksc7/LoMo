#include "ingestor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lomo_os.h"

typedef struct {
    const uint64_t* ts;
    const uint8_t* symbols;
    const size_t* sym_offsets;
} SortContext;

static SortContext g_ctx;

static int compare_multi(const void* a, const void* b) {
    uint32_t ia = *(const uint32_t*)a;
    uint32_t ib = *(const uint32_t*)b;
    
    // 1. Symbol Comparison
    const uint8_t* sa = g_ctx.symbols + g_ctx.sym_offsets[ia];
    const uint8_t* sb = g_ctx.symbols + g_ctx.sym_offsets[ib];
    uint64_t lena = *(uint64_t*)sa;
    uint64_t lenb = *(uint64_t*)sb;
    size_t min_len = (size_t)(lena < lenb ? lena : lenb);
    int cmp = memcmp(sa + 8, sb + 8, min_len);
    if (cmp != 0) return cmp;
    if (lena < lenb) return -1;
    if (lena > lenb) return 1;

    // 2. Timestamp Comparison
    if (g_ctx.ts[ia] < g_ctx.ts[ib]) return -1;
    if (g_ctx.ts[ia] > g_ctx.ts[ib]) return 1;
    return 0;
}

LomoMemTable* lomo_init_memtable(uint32_t column_count, const LomoColumnType* types) {
    LomoMemTable* mt = (LomoMemTable*)malloc(sizeof(LomoMemTable));
    mt->column_count = column_count; mt->row_count = 0; mt->max_rows = LOMO_MEMTABLE_MAX_ROWS;
    mt->types = (LomoColumnType*)malloc(column_count * sizeof(LomoColumnType));
    memcpy(mt->types, types, column_count * sizeof(LomoColumnType));
    mt->column_buffers = (void**)malloc(column_count * sizeof(void*));
    mt->column_sizes = (size_t*)calloc(column_count, sizeof(size_t));
    mt->column_capacities = (size_t*)calloc(column_count, sizeof(size_t));
    for (uint32_t i = 0; i < column_count; i++) {
        mt->column_capacities[i] = 1024 * 1024;
        mt->column_buffers[i] = lomo_aligned_malloc(mt->column_capacities[i], 4096);
    }
    lomo_mutex_init(&mt->lock);
    return mt;
}

int lomo_ingest_row(LomoMemTable* mt, const void** column_data, const size_t* column_sizes) {
    if (!mt) return -1;
    lomo_mutex_lock(&mt->lock);
    if (mt->row_count >= mt->max_rows) {
        lomo_mutex_unlock(&mt->lock);
        return -1;
    }
    size_t total = 0;
    for (uint32_t i = 0; i < mt->column_count; i++) {
        size_t sz = column_sizes[i];
        size_t st = (mt->types[i] == LOMO_TYPE_STRING) ? (sz + 8) : sz;
        if (mt->column_sizes[i] + st > mt->column_capacities[i]) {
            size_t nc = mt->column_capacities[i] * 2;
            while (mt->column_sizes[i] + st > nc) nc *= 2;
            mt->column_buffers[i] = lomo_aligned_realloc(mt->column_buffers[i], nc, 4096);
            mt->column_capacities[i] = nc;
        }
        uint8_t* dst = (uint8_t*)mt->column_buffers[i] + mt->column_sizes[i];
        if (mt->types[i] == LOMO_TYPE_STRING) {
            uint64_t off = (uint64_t)sz; memcpy(dst, &off, 8); memcpy(dst + 8, column_data[i], sz);
        } else memcpy(dst, column_data[i], sz);
        mt->column_sizes[i] += st; total += mt->column_sizes[i];
    }
    mt->row_count++; 
    lomo_mutex_unlock(&mt->lock);
    return 0;
}

int lomo_flush_memtable(LomoMemTable* mt, const char* directory_path) {
    if (!mt || mt->row_count == 0) return 0;
    uint32_t* idxs = (uint32_t*)malloc(mt->row_count * 4);
    for (uint32_t i = 0; i < mt->row_count; i++) idxs[i] = i;

    // Prepare context for [Symbol, TS] sorting
    g_ctx.ts = (const uint64_t*)mt->column_buffers[0];
    g_ctx.symbols = (const uint8_t*)mt->column_buffers[1]; // Column 1 is Symbol
    size_t* sym_offs = (size_t*)malloc(mt->row_count * sizeof(size_t));
    size_t scan = 0;
    for (uint32_t r = 0; r < mt->row_count; r++) {
        sym_offs[r] = scan; scan += (8 + *(uint64_t*)(g_ctx.symbols + scan));
    }
    g_ctx.sym_offsets = sym_offs;

    printf("[LoMo Ingestor] Multi-Column Sorting [Symbol, TS] for %u rows...\n", mt->row_count);
    qsort(idxs, mt->row_count, 4, compare_multi);

    for (uint32_t i = 0; i < mt->column_count; i++) {
        void* sbuf = lomo_aligned_malloc(mt->column_capacities[i], 4096);
        size_t wo = 0;
        if (mt->types[i] == LOMO_TYPE_STRING) {
            size_t* o_offs = (size_t*)malloc(mt->row_count * sizeof(size_t));
            size_t sc = 0;
            for (uint32_t r = 0; r < mt->row_count; r++) {
                o_offs[r] = sc; sc += (8 + *(uint64_t*)((uint8_t*)mt->column_buffers[i] + sc));
            }
            for (uint32_t j = 0; j < mt->row_count; j++) {
                uint32_t oi = idxs[j]; size_t ln = 8 + *(uint64_t*)((uint8_t*)mt->column_buffers[i] + o_offs[oi]);
                memcpy((uint8_t*)sbuf + wo, (uint8_t*)mt->column_buffers[i] + o_offs[oi], ln);
                wo += ln;
            }
            free(o_offs);
        } else {
            size_t rs = mt->column_sizes[i] / mt->row_count;
            for (uint32_t j = 0; j < mt->row_count; j++) {
                memcpy((uint8_t*)sbuf + (j * rs), (uint8_t*)mt->column_buffers[i] + (idxs[j] * rs), rs);
            }
        }
        lomo_aligned_free(mt->column_buffers[i]); mt->column_buffers[i] = sbuf;
    }
    LomoPartHeader* p = lomo_init_part(mt->column_count); p->total_rows = mt->row_count;
    for (uint32_t i = 0; i < mt->column_count; i++) p->columns[i].type = mt->types[i];
    int res = lomo_flush_part(p, directory_path, mt->column_buffers, mt->column_sizes);
    lomo_free_part(p); free(idxs); free(sym_offs);
    for (uint32_t i = 0; i < mt->column_count; i++) mt->column_sizes[i] = 0;
    mt->row_count = 0; return res;
}

void lomo_free_memtable(LomoMemTable* mt) {
    if (!mt) return;
    lomo_mutex_destroy(&mt->lock);
    for (uint32_t i = 0; i < mt->column_count; i++) lomo_aligned_free(mt->column_buffers[i]);
    free(mt->column_buffers); free(mt->column_sizes); free(mt->column_capacities); free(mt->types); free(mt);
}

int lomo_save_memtable_snapshot(const LomoMemTable* mt, const char* file_path) {
    if (!mt || !file_path) return -1;
    FILE* fp = fopen(file_path, "wb");
    if (!fp) return -1;
    fwrite(&mt->column_count, 4, 1, fp);
    fwrite(mt->types, sizeof(LomoColumnType), mt->column_count, fp);
    fwrite(&mt->row_count, 4, 1, fp);
    for (uint32_t i = 0; i < mt->column_count; i++) {
        fwrite(&mt->column_sizes[i], sizeof(size_t), 1, fp);
        fwrite(mt->column_buffers[i], 1, mt->column_sizes[i], fp);
    }
    fclose(fp);
    return 0;
}

LomoMemTable* lomo_load_memtable_snapshot(const char* file_path) {
    if (!file_path) return NULL;
    FILE* fp = fopen(file_path, "rb");
    if (!fp) return NULL;
    uint32_t col_count;
    fread(&col_count, 4, 1, fp);
    LomoColumnType* types = (LomoColumnType*)malloc(col_count * sizeof(LomoColumnType));
    fread(types, sizeof(LomoColumnType), col_count, fp);
    LomoMemTable* mt = lomo_init_memtable(col_count, types);
    fread(&mt->row_count, 4, 1, fp);
    for (uint32_t i = 0; i < col_count; i++) {
        fread(&mt->column_sizes[i], sizeof(size_t), 1, fp);
        if (mt->column_sizes[i] > mt->column_capacities[i]) {
            mt->column_buffers[i] = lomo_aligned_realloc(mt->column_buffers[i], mt->column_sizes[i], 4096);
            mt->column_capacities[i] = mt->column_sizes[i];
        }
        fread(mt->column_buffers[i], 1, mt->column_sizes[i], fp);
    }
    free(types);
    fclose(fp);
    return mt;
}
