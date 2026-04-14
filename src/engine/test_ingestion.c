#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "ingestor.h"
#include "storage_engine.h"

int main() {
    printf("[LoMo Test] Starting ingestion test...\n");

    // 1. Define 3 columns (Timestamp, Level(Int), Message(String))
    LomoColumnType types[] = {LOMO_TYPE_TIMESTAMP, LOMO_TYPE_INT64, LOMO_TYPE_STRING};
    LomoMemTable* mt = lomo_init_memtable(3, types);
    if (!mt) {
        printf("Failed to init MemTable\n");
        return 1;
    }

    // 2. Ingest some dummy rows
    for (int i = 0; i < 100; i++) {
        uint64_t ts = (uint64_t)time(NULL) + i;
        uint64_t level = (uint64_t)(rand() % 4);
        const char* msg = "Test Log Message Example";
        size_t msg_len = 24;

        const void* row_data[] = {&ts, &level, msg};
        size_t row_sizes[] = {sizeof(uint64_t), sizeof(uint64_t), msg_len};

        lomo_ingest_row(mt, row_data, row_sizes);
    }

    printf("[LoMo Test] Ingested 100 rows into MemTable.\n");

    // 3. Flush to disk
    int res = lomo_flush_memtable(mt, "test_part_dir");
    if (res == 0) {
        printf("[LoMo Test] Flush SUCCESS: Check 'test_part_dir' folder.\n");
    } else {
        printf("[LoMo Test] Flush FAILED with code %d.\n", res);
    }

    lomo_free_memtable(mt);
    return 0;
}
