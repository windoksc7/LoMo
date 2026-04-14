#include "storage_engine.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <compressapi.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Compression Helpers
static size_t compress_data(const void* src, size_t src_size, void* dst, size_t dst_cap) {
    COMPRESSOR_HANDLE compressor = NULL;
    size_t compressed_size = 0;
    if (CreateCompressor(COMPRESS_ALGORITHM_XPRESS, NULL, &compressor)) {
        Compress(compressor, src, src_size, dst, dst_cap, &compressed_size);
        CloseCompressor(compressor);
    }
    return compressed_size;
}

static size_t decompress_data(const void* src, size_t src_size, void* dst, size_t dst_cap) {
    DECOMPRESSOR_HANDLE decompressor = NULL;
    size_t decompressed_size = 0;
    if (CreateDecompressor(COMPRESS_ALGORITHM_XPRESS, NULL, &decompressor)) {
        Decompress(decompressor, src, src_size, dst, dst_cap, &decompressed_size);
        CloseDecompressor(decompressor);
    }
    return decompressed_size;
}

// Initialize a new part for writing (MemTable phase)
LomoPartHeader* lomo_init_part(uint32_t column_count) {
    LomoPartHeader* part = (LomoPartHeader*)malloc(sizeof(LomoPartHeader));
    if (!part) return NULL;

    part->magic = LOMO_MAGIC;
    part->version = LOMO_VERSION;
    part->total_rows = 0;
    part->column_count = column_count;
    part->granule_count = 0;
    part->index = NULL;
    part->directory_path = NULL;

    if (column_count > 0) {
        part->columns = (LomoColumnMeta*)calloc(column_count, sizeof(LomoColumnMeta));
        if (!part->columns) {
            free(part);
            return NULL;
        }
    } else {
        part->columns = NULL;
    }

    return part;
}

// Append a block of data to a specific column
int lomo_write_column_chunk(LomoPartHeader* part, uint32_t column_id, const void* data_ptr, size_t size) {
    if (!part || !part->columns || column_id >= part->column_count || !data_ptr || size == 0) {
        return -1; // Error: Invalid arguments
    }
    
    // In current implementation, ingestor handles the MemTable.
    return 0; // Success
}

// Read a block of column data into an AVX2/SIMD aligned buffer
int lomo_read_column_chunk_simd(const LomoPartHeader* part, uint32_t column_id, uint64_t granule_idx, void* aligned_buffer, size_t buffer_size) {
    if (!part || !part->index || granule_idx >= part->granule_count) return -1;
    
    LomoSparseIndexGranule* g = &part->index[granule_idx];
    char file_path[256];
    sprintf(file_path, "%s/%u.col", part->directory_path ? part->directory_path : ".", column_id); 
    
    FILE* fp = fopen(file_path, "rb");
    if (!fp) return -1;

    fseek(fp, (long)g->start_offset, SEEK_SET);
    
    if (part->columns[column_id].compression == LOMO_COMPRESS_XPRESS) {
        void* comp_buf = malloc(g->compressed_size);
        fread(comp_buf, 1, g->compressed_size, fp);
        decompress_data(comp_buf, g->compressed_size, aligned_buffer, buffer_size);
        free(comp_buf);
    } else {
        fread(aligned_buffer, 1, g->uncompressed_size, fp);
    }
    
    fclose(fp);
    return 0;
}

int lomo_read_column_simd(const LomoPartHeader* part, uint32_t column_id, void* aligned_buffer, size_t buffer_size) {
    if (!part || column_id >= part->column_count) return -1;
    
    char file_path[256];
    sprintf(file_path, "%s/%u.col", part->directory_path ? part->directory_path : ".", column_id); 
    
    FILE* fp = fopen(file_path, "rb");
    if (!fp) return -1;

    if (part->columns[column_id].compression == LOMO_COMPRESS_XPRESS) {
        // Must read granule by granule because each granule is a separate compression stream
        uint8_t* dst = (uint8_t*)aligned_buffer;
        for (uint32_t g = 0; g < part->granule_count; g++) {
            LomoSparseIndexGranule* gr = &part->index[g];
            fseek(fp, (long)gr->start_offset, SEEK_SET);
            void* comp_buf = malloc(gr->compressed_size);
            fread(comp_buf, 1, gr->compressed_size, fp);
            decompress_data(comp_buf, gr->compressed_size, dst, gr->uncompressed_size);
            dst += gr->uncompressed_size;
            free(comp_buf);
        }
    } else {
        fread(aligned_buffer, 1, buffer_size, fp);
    }
    
    fclose(fp);
    return 0;
}

// Flush the MemTable to disk with Compression
int lomo_flush_part(LomoPartHeader* part, const char* directory_path, void** column_buffers, size_t* column_sizes) {
    if (!part || !directory_path || !column_buffers || !column_sizes) return -1;
    mkdir(directory_path, 0777);

    if (part->directory_path) free(part->directory_path);
    part->directory_path = _strdup(directory_path);

    uint32_t num_granules = (uint32_t)((part->total_rows + LOMO_GRANULE_ROWS - 1) / LOMO_GRANULE_ROWS);
    part->granule_count = num_granules;
    part->index = (LomoSparseIndexGranule*)calloc(num_granules, sizeof(LomoSparseIndexGranule));

    for (uint32_t c = 0; c < part->column_count; c++) {
        char file_path[256];
        sprintf(file_path, "%s/%u.col", directory_path, c);
        FILE* fp = fopen(file_path, "wb");
        if (!fp) continue;

        // For Phase 1 MVP, we only compress the primary column to simplify indexing
        if (c == 0) {
            part->columns[c].compression = LOMO_COMPRESS_XPRESS;
        } else {
            part->columns[c].compression = LOMO_COMPRESS_NONE;
        }
        uint64_t current_file_offset = 0;
        uint8_t* col_base = (uint8_t*)column_buffers[c];

        // Process column per granule
        for (uint32_t g = 0; g < num_granules; g++) {
            uint32_t start_row = g * LOMO_GRANULE_ROWS;
            uint32_t end_row = (g + 1) * LOMO_GRANULE_ROWS;
            if (end_row > part->total_rows) end_row = (uint32_t)part->total_rows;
            uint32_t rows_in_g = end_row - start_row;

            // Calculate uncompressed bytes for this granule
            size_t uncomp_size = 0;
            size_t src_offset = 0;
            if (part->columns[c].type == LOMO_TYPE_STRING) {
                // Scan to find byte range for this granule's strings
                uint8_t* p = col_base;
                for(uint32_t r=0; r<start_row; r++) p += (sizeof(uint64_t) + *(uint64_t*)p);
                src_offset = p - col_base;
                uint8_t* start_p = p;
                for(uint32_t r=0; r<rows_in_g; r++) p += (sizeof(uint64_t) + *(uint64_t*)p);
                uncomp_size = p - start_p;
            } else {
                size_t row_sz = column_sizes[c] / part->total_rows;
                src_offset = start_row * row_sz;
                uncomp_size = rows_in_g * row_sz;
            }

            if (part->columns[c].compression == LOMO_COMPRESS_XPRESS) {
                // Compress block
                void* comp_buf = malloc(uncomp_size + 1024); // Padding for safety
                size_t comp_size = compress_data(col_base + src_offset, uncomp_size, comp_buf, uncomp_size + 1024);

                if (comp_size > 0 && comp_size < uncomp_size) {
                    fwrite(comp_buf, 1, comp_size, fp);
                    if (c == 0) { // Primary index uses col 0
                        part->index[g].start_offset = current_file_offset;
                        part->index[g].compressed_size = (uint32_t)comp_size;
                        part->index[g].uncompressed_size = (uint32_t)uncomp_size;
                    }
                    current_file_offset += comp_size;
                } else {
                    // Fallback to no compression if it didn't help
                    fwrite(col_base + src_offset, 1, uncomp_size, fp);
                    if (c == 0) {
                        part->index[g].start_offset = current_file_offset;
                        part->index[g].compressed_size = (uint32_t)uncomp_size;
                        part->index[g].uncompressed_size = (uint32_t)uncomp_size;
                    }
                    current_file_offset += uncomp_size;
                }
                free(comp_buf);
            } else {
                // No compression for this column
                fwrite(col_base + src_offset, 1, uncomp_size, fp);
                current_file_offset += uncomp_size;
            }

            // Update timestamp range for index (only for column 0)
            if (c == 0) {
                uint64_t* ts = (uint64_t*)column_buffers[0];
                part->index[g].min_timestamp = ts[start_row];
                part->index[g].max_timestamp = ts[end_row - 1];
                part->index[g].row_count = rows_in_g;
            }
        }
        fclose(fp);
        part->columns[c].column_id = c;
        part->columns[c].uncompressed_size = column_sizes[c];
        part->columns[c].compressed_size = current_file_offset;
    }

    // Write primary.idx
    char idx_path[256]; sprintf(idx_path, "%s/primary.idx", directory_path);
    FILE* ifp = fopen(idx_path, "wb");
    if (ifp) { fwrite(part->index, sizeof(LomoSparseIndexGranule), num_granules, ifp); fclose(ifp); }

    // Write header
    char h_path[256]; sprintf(h_path, "%s/header.bin", directory_path);
    FILE* hfp = fopen(h_path, "wb");
    if (hfp) {
        fwrite(&part->magic, 4, 1, hfp); fwrite(&part->version, 4, 1, hfp);
        fwrite(&part->total_rows, 8, 1, hfp); fwrite(&part->column_count, 4, 1, hfp);
        for (uint32_t i = 0; i < part->column_count; i++) fwrite(&part->columns[i], sizeof(LomoColumnMeta), 1, hfp);
        fwrite(&part->granule_count, 4, 1, hfp);
        fclose(hfp);
    }
    
    printf("[LoMo Storage] Flushed compressed part to %s\n", directory_path);
    return 0;
}

// Load a part header from a directory
LomoPartHeader* lomo_load_part(const char* directory_path) {
    char header_path[256];
    sprintf(header_path, "%s/header.bin", directory_path);
    
    FILE* fp = fopen(header_path, "rb");
    if (!fp) return NULL;

    LomoPartHeader* part = (LomoPartHeader*)malloc(sizeof(LomoPartHeader));
    if (!part) {
        fclose(fp);
        return NULL;
    }
    part->directory_path = _strdup(directory_path);

    fread(&part->magic, sizeof(uint32_t), 1, fp);
    fread(&part->version, sizeof(uint32_t), 1, fp);
    fread(&part->total_rows, sizeof(uint64_t), 1, fp);
    fread(&part->column_count, sizeof(uint32_t), 1, fp);

    if (part->magic != LOMO_MAGIC) {
        free(part);
        fclose(fp);
        return NULL;
    }

    part->columns = (LomoColumnMeta*)calloc(part->column_count, sizeof(LomoColumnMeta));
    for (uint32_t i = 0; i < part->column_count; i++) {
        fread(&part->columns[i], sizeof(LomoColumnMeta), 1, fp);
    }

    if (fread(&part->granule_count, sizeof(uint32_t), 1, fp) == 1) {
        char idx_path[256];
        sprintf(idx_path, "%s/primary.idx", directory_path);
        FILE* ifp = fopen(idx_path, "rb");
        if (ifp) {
            part->index = (LomoSparseIndexGranule*)calloc(part->granule_count, sizeof(LomoSparseIndexGranule));
            fread(part->index, sizeof(LomoSparseIndexGranule), part->granule_count, ifp);
            fclose(ifp);
        } else {
            part->index = NULL;
        }
    } else {
        part->granule_count = 0;
        part->index = NULL;
    }

    fclose(fp);
    return part;
}

uint32_t* lomo_filter_granules_by_time(const LomoPartHeader* part, uint64_t min_ts, uint64_t max_ts, uint32_t* out_count) {
    if (!part || !part->index || part->granule_count == 0) return NULL;

    uint32_t* matched = (uint32_t*)malloc(part->granule_count * sizeof(uint32_t));
    uint32_t count = 0;

    for (uint32_t i = 0; i < part->granule_count; i++) {
        // Range overlap check
        if (!(part->index[i].max_timestamp < min_ts || part->index[i].min_timestamp > max_ts)) {
            matched[count++] = i;
        }
    }

    *out_count = count;
    return matched;
}

// Free resources associated with the part header
void lomo_free_part(LomoPartHeader* part) {
    if (!part) return;
    
    if (part->columns) {
        free(part->columns);
    }
    
    if (part->index) {
        free(part->index);
    }
    
    if (part->directory_path) {
        free(part->directory_path);
    }
    
    free(part);
}
