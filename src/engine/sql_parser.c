#include "lomo_os.h"
#include "sql_parser.h"
#include "simd_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Simple tokenizer helpers
static const char* skip_ws(const char* s) { while(*s && isspace(*s)) s++; return s; }
static int match_word(const char* s, const char* word) {
    size_t len = strlen(word);
    return _strnicmp(s, word, len) == 0 && !isalnum(s[len]);
}

LomoQueryPlan lomo_parse_sql(const char* sql) {
    LomoQueryPlan plan;
    memset(&plan, 0, sizeof(plan));
    const char* cur = skip_ws(sql);

    // 1. Parse SELECT
    if (match_word(cur, "SELECT")) {
        cur = skip_ws(cur + 6);
        // Handle SELECT SUM(col), MAX(col), COUNT(*)
        // Simplified: assume only one agg for MVP
        if (match_word(cur, "SUM")) {
            strcpy(plan.aggs[0].func, "SUM");
            cur = strchr(cur, '(') + 1;
            plan.aggs[0].column_id = (uint32_t)atoi(cur);
            plan.agg_count = 1;
            cur = strchr(cur, ')') + 1;
        } else if (match_word(cur, "COUNT")) {
            strcpy(plan.aggs[0].func, "COUNT");
            plan.agg_count = 1;
            cur = strchr(cur, ')') + 1;
        }
    }

    // 2. Parse FROM
    cur = skip_ws(cur);
    if (match_word(cur, "FROM")) {
        cur = skip_ws(cur + 4);
        const char* end = cur;
        while(*end && !isspace(*end)) end++;
        size_t len = end - cur;
        if (cur[0] == '\'') { cur++; len-=2; } // Strip quotes
        strncpy(plan.table_path, cur, len);
        cur = end;
    }

    // 3. Parse WHERE
    cur = skip_ws(cur);
    if (match_word(cur, "WHERE")) {
        cur = skip_ws(cur + 5);
        // Handle WHERE col > val
        // Simplified: single filter
        plan.filters[0].column_id = (uint32_t)atoi(cur);
        while(isdigit(*cur)) cur++;
        cur = skip_ws(cur);
        if (*cur == '>') {
            plan.filters[0].op = LOMO_OP_GT;
            cur = skip_ws(cur + 1);
            plan.filters[0].val_int = atoll(cur);
            plan.filter_count = 1;
        } else if (match_word(cur, "CONTAINS")) {
            plan.filters[0].op = LOMO_OP_CONTAINS;
            cur = skip_ws(cur + 8);
            if (*cur == '\'') cur++;
            const char* end = strchr(cur, '\'');
            if (end) {
                strncpy(plan.filters[0].val_str, cur, end - cur);
                plan.filter_count = 1;
            }
        }
    }

    return plan;
}

// Internal helper to apply filters and generate a bitmask
static void lomo_apply_filters(const LomoQueryPlan* plan, const LomoPartHeader* part, uint8_t* mask) {
    uint64_t total_rows = part->total_rows;
    memset(mask, 0xFF, (size_t)((total_rows + 7) / 8));

    for (uint32_t i = 0; i < plan->filter_count; i++) {
        uint32_t cid = plan->filters[i].column_id;
        if (cid >= part->column_count) continue;

        size_t col_size = (size_t)part->columns[cid].uncompressed_size;
        void* buffer = lomo_aligned_malloc(col_size, 32);
        if (!buffer) continue;
        
        if (lomo_read_column_simd(part, cid, buffer, col_size) == 0) {
            if (plan->filters[i].op == LOMO_OP_GT) {
                uint8_t* new_mask = (uint8_t*)calloc((size_t)((total_rows + 7) / 8), 1);
                if (new_mask) {
                    lomo_simd_filter_int64_gt_mask((const int64_t*)buffer, total_rows, plan->filters[i].val_int, new_mask);
                    for(size_t b=0; b<(total_rows+7)/8; b++) mask[b] &= new_mask[b];
                    free(new_mask);
                }
            } else if (plan->filters[i].op == LOMO_OP_CONTAINS) {
                uint8_t* new_mask = (uint8_t*)calloc((size_t)((total_rows + 7) / 8), 1);
                if (new_mask) {
                    lomo_simd_filter_string_contains_mask((const char*)buffer, total_rows, mask, new_mask, plan->filters[i].val_str, strlen(plan->filters[i].val_str));
                    memcpy(mask, new_mask, (size_t)((total_rows+7)/8));
                    free(new_mask);
                }
            }
        }
        lomo_aligned_free(buffer);
    }
}

// Internal helper to run aggregations using the filter mask
static void lomo_run_aggregations(const LomoQueryPlan* plan, const LomoPartHeader* part, const uint8_t* mask) {
    uint64_t total_rows = part->total_rows;

    for (uint32_t i = 0; i < plan->agg_count; i++) {
        if (strcmp(plan->aggs[i].func, "SUM") == 0) {
            uint32_t cid = plan->aggs[i].column_id;
            if (cid >= part->column_count) continue;

            size_t sz = (size_t)part->columns[cid].uncompressed_size;
            int64_t* buf = (int64_t*)lomo_aligned_malloc(sz, 32);
            if (buf) {
                if (lomo_read_column_simd(part, cid, buf, sz) == 0) {
                    int64_t result = lomo_simd_sum_int64_masked(buf, total_rows, mask);
                    printf("RESULT SUM(col %u): %lld\n", cid, (long long)result);
                }
                lomo_aligned_free(buf);
            }
        } else if (strcmp(plan->aggs[i].func, "COUNT") == 0) {
            uint64_t count = 0;
            for(size_t r=0; r<total_rows; r++) {
                if (mask[r/8] & (1<<(r%8))) count++;
            }
            printf("RESULT COUNT(*): %llu\n", (unsigned long long)count);
        }
    }
}

void lomo_execute_plan(const LomoQueryPlan* plan) {
    printf("[LoMo SQL] Executing Query on: %s\n", plan->table_path);
    
    LomoPartHeader* part = lomo_load_part(plan->table_path);
    if (!part) { 
        printf("Error: Table not found at path '%s'\n", plan->table_path); 
        return; 
    }

    uint8_t* mask = (uint8_t*)calloc((size_t)((part->total_rows + 7) / 8), 1);
    if (!mask) {
        lomo_free_part(part);
        return;
    }

    // 1. Filter Phase
    lomo_apply_filters(plan, part, mask);

    // 2. Aggregation Phase
    lomo_run_aggregations(plan, part, mask);

    free(mask);
    lomo_free_part(part);
}
