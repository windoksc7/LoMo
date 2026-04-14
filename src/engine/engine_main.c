#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "storage_engine.h"
#include "sql_parser.h"
#include "ingestor.h"

#define MAX_INPUT_LEN 1024

void print_help() {
    printf("\nLoMo Phase 1 CLI - Commands:\n");
    printf("  help              - Show this help\n");
    printf("  exit              - Quit the application\n");
    printf("  status            - Show engine status (ingestion, storage)\n");
    printf("  query <SQL>       - Execute a SQL query (e.g., query SELECT COUNT(*) FROM test_part_dir)\n");
    printf("  gen <rows>        - Generate dummy log data for testing\n");
    printf("\n");
}

void generate_test_data(const char* path, uint32_t rows) {
    LomoColumnType types[] = { LOMO_TYPE_TIMESTAMP, LOMO_TYPE_INT64, LOMO_TYPE_STRING };
    LomoMemTable* mt = lomo_init_memtable(3, types);
    
    printf("[LoMo CLI] Generating %u rows of test data...\n", rows);
    uint64_t start_time = (uint64_t)time(NULL);

    for (uint32_t i = 0; i < rows; i++) {
        uint64_t ts = start_time + i;
        int64_t val = (int64_t)rand() % 1000;
        const char* msg = "Example log message for LoMo Phase 1 testing";
        
        const void* data[] = { &ts, &val, msg };
        size_t sizes[] = { sizeof(uint64_t), sizeof(int64_t), strlen(msg) };
        lomo_ingest_row(mt, data, sizes);
    }

    lomo_flush_memtable(mt, path);
    lomo_free_memtable(mt);
    printf("[LoMo CLI] Test data generated in %s\n", path);
}

int main() {
    char input[MAX_INPUT_LEN];
    printf("========================================\n");
    printf("   LoMo Unified Industrial Data Platform\n");
    printf("       Phase 1: High-Speed Analytics\n");
    printf("========================================\n");
    print_help();

    while (1) {
        printf("lomo> ");
        if (!fgets(input, MAX_INPUT_LEN, stdin)) break;
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        if (strlen(input) == 0) continue;

        if (_strnicmp(input, "exit", 4) == 0) {
            break;
        } else if (_strnicmp(input, "help", 4) == 0) {
            print_help();
        } else if (_strnicmp(input, "status", 6) == 0) {
            printf("[LoMo Status] Engine: ONLINE\n");
            printf("[LoMo Status] Storage Mode: Columnar MergeTree\n");
            printf("[LoMo Status] Compression: XPRESS (Windows Native)\n");
            printf("[LoMo Status] Granule Size: %d rows\n", LOMO_GRANULE_ROWS);
        } else if (_strnicmp(input, "query ", 6) == 0) {
            LomoQueryPlan plan = lomo_parse_sql(input + 6);
            if (strlen(plan.table_path) > 0) {
                lomo_execute_plan(&plan);
            } else {
                printf("Error: Invalid SQL syntax or missing table path.\n");
            }
        } else if (_strnicmp(input, "gen ", 4) == 0) {
            uint32_t rows = (uint32_t)atoi(input + 4);
            if (rows == 0) rows = 1000;
            generate_test_data("test_part_dir", rows);
        } else {
            printf("Unknown command: %s. Type 'help' for available commands.\n", input);
        }
    }

    printf("Goodbye!\n");
    return 0;
}
