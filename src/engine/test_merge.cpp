#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <windows.h>
#include "ingestor.h"
#include "merger.h"

void create_dummy_part(const char* dir, int start_offset, int row_count) {
    LomoColumnType types[] = {LOMO_TYPE_TIMESTAMP, LOMO_TYPE_INT64, LOMO_TYPE_STRING};
    LomoMemTable* mt = lomo_init_memtable(3, types);
    
    for (int i = 0; i < row_count; i++) {
        uint64_t ts = (uint64_t)1000000 + start_offset + i;
        uint64_t val = (uint64_t)i;
        const char* msg = "Merge Test Message";
        size_t msg_len = 18;

        const void* data[] = {&ts, &val, msg};
        size_t sizes[] = {8, 8, msg_len};
        lomo_ingest_row(mt, data, sizes);
    }
    lomo_flush_memtable(mt, dir);
    lomo_free_memtable(mt);
}

int main() {
    printf("[LoMo Test] Creating 2 parts for merging...\n");
    
    // Part 1: Even timestamps
    // Part 2: Odd timestamps (to test interleaved merge)
    LomoColumnType types[] = {LOMO_TYPE_TIMESTAMP, LOMO_TYPE_INT64, LOMO_TYPE_STRING};
    LomoMemTable* mt1 = lomo_init_memtable(3, types);
    LomoMemTable* mt2 = lomo_init_memtable(3, types);

    for(int i=0; i<50; i++) {
        uint64_t ts1 = (uint64_t)2000 + (i * 2);     // 2000, 2002...
        uint64_t ts2 = (uint64_t)2000 + (i * 2) + 1; // 2001, 2003...
        uint64_t val = 100;
        const char* msg = "Part Data";
        
        const void* d1[] = {&ts1, &val, msg};
        const void* d2[] = {&ts2, &val, msg};
        size_t s[] = {8, 8, 9};
        
        lomo_ingest_row(mt1, d1, s);
        lomo_ingest_row(mt2, d2, s);
    }

    lomo_flush_memtable(mt1, "part_even");
    lomo_flush_memtable(mt2, "part_odd");
    
    lomo_free_memtable(mt1);
    lomo_free_memtable(mt2);

    const char* sources[] = {"part_even", "part_odd"};
    lomo_merge_parts(sources, 2, "merged_part");

    printf("[LoMo Test] Merge complete. Verifying merged part...\n");
    // You can use query_lomo.exe merged_part later to verify
    
    return 0;
}
