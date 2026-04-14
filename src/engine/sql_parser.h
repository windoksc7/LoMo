#ifndef LOMO_SQL_PARSER_H
#define LOMO_SQL_PARSER_H

#include <stdint.h>
#include "storage_engine.h"

typedef enum {
    LOMO_OP_NONE,
    LOMO_OP_GT,
    LOMO_OP_BETWEEN,
    LOMO_OP_CONTAINS
} LomoFilterOp;

typedef struct {
    uint32_t column_id;
    LomoFilterOp op;
    int64_t val_int;
    uint64_t val_range_max; // For BETWEEN
    char val_str[256];      // For CONTAINS
} LomoFilter;

typedef struct {
    char table_path[256];
    LomoFilter filters[8];
    uint32_t filter_count;
    
    struct {
        uint32_t column_id;
        char func[16]; // "SUM", "MAX", "COUNT"
    } aggs[8];
    uint32_t agg_count;
} LomoQueryPlan;

LomoQueryPlan lomo_parse_sql(const char* sql);
void lomo_execute_plan(const LomoQueryPlan* plan);

#endif // LOMO_SQL_PARSER_H
