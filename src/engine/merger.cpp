#include "merger.h"
#include "ingestor.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <vector>
#include <filesystem>
#include <chrono>
#include "lomo_os.h"

namespace fs = std::filesystem;

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Simplified Merger for Phase 1: Loads all source data into memory and re-flushes.
int lomo_merge_parts(const char** part_paths, uint32_t part_count, const char* output_path) {
    if (part_count == 0) return 0;
    mkdir(output_path, 0777);

    printf("[LoMo Merger] Merging %u parts into %s...\n", part_count, output_path);

    uint32_t column_count = 0;
    LomoColumnType* types = NULL;
    uint64_t total_rows = 0;

    // 1. Load all parts and calculate total capacity
    std::vector<LomoPartHeader*> headers;
    for (uint32_t p = 0; p < part_count; p++) {
        LomoPartHeader* h = lomo_load_part(part_paths[p]);
        if (!h) continue;
        if (headers.empty()) {
            column_count = h->column_count;
            types = (LomoColumnType*)malloc(column_count * sizeof(LomoColumnType));
            for(uint32_t c=0; c<column_count; c++) types[c] = h->columns[c].type;
        }
        total_rows += h->total_rows;
        headers.push_back(h);
    }

    if (headers.empty()) return -1;

    // 2. Initialize a MemTable to hold all merged data
    LomoMemTable* mt = lomo_init_memtable(column_count, types);
    mt->max_rows = (uint32_t)total_rows;

    // 3. For each part, read all data and ingest into MemTable
    for (auto h : headers) {
        void** p_bufs = (void**)malloc(column_count * sizeof(void*));
        size_t* p_sizes = (size_t*)malloc(column_count * sizeof(size_t));

        for (uint32_t c = 0; c < column_count; c++) {
            p_sizes[c] = (size_t)h->columns[c].uncompressed_size;
            p_bufs[c] = lomo_aligned_malloc(p_sizes[c], 4096);
            lomo_read_column_simd(h, c, p_bufs[c], p_sizes[c]);
        }

        uint32_t* col_offsets = (uint32_t*)calloc(column_count, sizeof(uint32_t));
        for (uint32_t r = 0; r < h->total_rows; r++) {
            const void* row_data[32]; 
            size_t row_sizes[32];

            for (uint32_t c = 0; c < column_count; c++) {
                if (types[c] == LOMO_TYPE_STRING) {
                    uint64_t len = *(uint64_t*)((uint8_t*)p_bufs[c] + col_offsets[c]);
                    row_data[c] = (uint8_t*)p_bufs[c] + col_offsets[c] + sizeof(uint64_t);
                    row_sizes[c] = (size_t)len;
                    col_offsets[c] += (uint32_t)(sizeof(uint64_t) + len);
                } else {
                    size_t sz = p_sizes[c] / h->total_rows;
                    row_data[c] = (uint8_t*)p_bufs[c] + (r * sz);
                    row_sizes[c] = sz;
                }
            }
            lomo_ingest_row(mt, row_data, row_sizes);
        }

        for (uint32_t c = 0; c < column_count; c++) lomo_aligned_free(p_bufs[c]);
        free(p_bufs);
        free(p_sizes);
        free(col_offsets);
        lomo_free_part(h);
    }

    // 4. Flush the combined MemTable
    int result = lomo_flush_memtable(mt, output_path);

    lomo_free_memtable(mt);
    free(types);

    return result;
}

// --- Background Merger Daemon Implementation ---

struct LomoMergerDaemon {
    std::atomic<bool> running;
    std::jthread worker;
    std::string watch_dir;
    std::string output_dir;
};

void lomo_merger_worker_loop(LomoMergerDaemon* daemon) {
    int merged_count = 0;
    while (daemon->running) {
        std::vector<std::string> parts_to_merge;
        try {
            if (fs::exists(daemon->watch_dir)) {
                for (const auto& entry : fs::directory_iterator(daemon->watch_dir)) {
                    if (entry.is_directory()) {
                        // Check if it's a valid part (has header.bin)
                        if (fs::exists(entry.path() / "header.bin")) {
                            parts_to_merge.push_back(entry.path().string());
                        }
                    }
                    if (parts_to_merge.size() >= 5) break; 
                }
            }
        } catch (...) {}

        if (parts_to_merge.size() >= 5) {
            char merged_path[512];
            sprintf(merged_path, "%s/merged_%d", daemon->output_dir.c_str(), merged_count++);
            
            std::vector<const char*> paths;
            for (const auto& p : parts_to_merge) paths.push_back(p.c_str());

            printf("[LoMo Daemon] Background merge starting for %u parts...\n", (uint32_t)paths.size());
            if (lomo_merge_parts(paths.data(), (uint32_t)paths.size(), merged_path) == 0) {
                // Cleanup originals
                for (const auto& p : parts_to_merge) {
                    try { fs::remove_all(p); } catch (...) {}
                }
                printf("[LoMo Daemon] Merge successful. Cleaned up %u small parts.\n", (uint32_t)parts_to_merge.size());
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

LomoMergerDaemon* lomo_start_merger_daemon(const char* watch_dir, const char* output_dir) {
    LomoMergerDaemon* daemon = new LomoMergerDaemon();
    daemon->running = true;
    daemon->watch_dir = watch_dir;
    daemon->output_dir = output_dir;
    
    // Ensure output dir exists
    try { fs::create_directories(output_dir); } catch (...) {}

    daemon->worker = std::jthread(lomo_merger_worker_loop, daemon);
    printf("[LoMo Daemon] Merger daemon started. Watching: %s\n", watch_dir);
    return daemon;
}

void lomo_stop_merger_daemon(LomoMergerDaemon* daemon) {
    if (!daemon) return;
    daemon->running = false;
    // jthread joins automatically
    delete daemon;
    printf("[LoMo Daemon] Merger daemon stopped.\n");
}
