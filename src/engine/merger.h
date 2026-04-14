#ifndef LOMO_MERGER_H
#define LOMO_MERGER_H

#include "storage_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Merges multiple parts into a single larger part.
 * @param part_paths Array of directory paths for source parts.
 * @param part_count Number of parts to merge.
 * @param output_path Directory for the new merged part.
 * @return 0 on success, non-zero on error.
 */
int lomo_merge_parts(const char** part_paths, uint32_t part_count, const char* output_path);

#ifdef __cplusplus
}
#endif

#endif // LOMO_MERGER_H
