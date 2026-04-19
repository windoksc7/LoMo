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

// --- Phase 2: Background Merger Daemon ---

typedef struct LomoMergerDaemon LomoMergerDaemon;

/**
 * @brief Starts a background daemon to merge small parts into larger ones.
 * @param watch_dir The directory to monitor for new small parts.
 * @param output_dir The directory to store the merged parts.
 * @return Handle to the merger daemon.
 */
LomoMergerDaemon* lomo_start_merger_daemon(const char* watch_dir, const char* output_dir);

/**
 * @brief Stops the background merger daemon and waits for it to finish its current merge.
 */
void lomo_stop_merger_daemon(LomoMergerDaemon* daemon);

#ifdef __cplusplus
}
#endif

#endif // LOMO_MERGER_H
