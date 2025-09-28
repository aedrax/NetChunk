/**
 * @file repair.h
 * @brief NetChunk repair engine
 *
 * Automatic chunk repair and replication management for NetChunk.
 * Handles integrity verification, missing chunk detection, and
 * automatic recovery from available replicas.
 */

#ifndef NETCHUNK_REPAIR_H
#define NETCHUNK_REPAIR_H

#include "config.h"
#include "crypto.h"
#include "ftp_client.h"
#include "manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Repair operation types
 */
typedef enum {
    NETCHUNK_REPAIR_VERIFY_ONLY = 0, // Only verify, don't repair
    NETCHUNK_REPAIR_AUTO = 1, // Automatic repair of issues found
    NETCHUNK_REPAIR_FORCE = 2 // Force repair regardless of status
} netchunk_repair_mode_t;

/**
 * @brief Chunk health status
 */
typedef enum {
    NETCHUNK_CHUNK_HEALTHY = 0, // All replicas available and valid
    NETCHUNK_CHUNK_DEGRADED = 1, // Some replicas missing/corrupted
    NETCHUNK_CHUNK_CRITICAL = 2, // Only one replica remaining
    NETCHUNK_CHUNK_LOST = 3 // No valid replicas found
} netchunk_chunk_health_t;

/**
 * @brief Repair statistics
 */
typedef struct {
    uint32_t chunks_verified; // Total chunks verified
    uint32_t chunks_healthy; // Chunks with full replication
    uint32_t chunks_degraded; // Chunks missing some replicas
    uint32_t chunks_critical; // Chunks with only one replica
    uint32_t chunks_lost; // Chunks with no valid replicas
    uint32_t chunks_repaired; // Chunks successfully repaired
    uint32_t replicas_added; // New replicas created
    uint32_t replicas_removed; // Corrupted replicas removed
    double elapsed_seconds; // Total repair time
} netchunk_repair_stats_t;

/**
 * @brief Repair progress callback
 *
 * @param userdata User-provided data pointer
 * @param current_chunk Current chunk being processed
 * @param total_chunks Total chunks to process
 * @param stats Current repair statistics
 */
typedef void (*netchunk_repair_progress_callback_t)(
    void* userdata,
    uint32_t current_chunk,
    uint32_t total_chunks,
    const netchunk_repair_stats_t* stats);

/**
 * @brief Repair engine context
 */
typedef struct {
    netchunk_config_t* config; // Configuration
    netchunk_ftp_context_t* ftp_context; // FTP client context
    netchunk_repair_progress_callback_t progress_cb; // Progress callback
    void* progress_userdata; // Progress callback data
    netchunk_repair_mode_t repair_mode; // Current repair mode
    bool initialized; // Initialization flag
} netchunk_repair_context_t;

/**
 * @brief Initialize repair engine context
 *
 * @param context Repair context to initialize
 * @param config NetChunk configuration
 * @param ftp_context FTP client context
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_init(
    netchunk_repair_context_t* context,
    netchunk_config_t* config,
    netchunk_ftp_context_t* ftp_context);

/**
 * @brief Set repair progress callback
 *
 * @param context Repair context
 * @param callback Progress callback function
 * @param userdata User data for callback
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_set_progress_callback(
    netchunk_repair_context_t* context,
    netchunk_repair_progress_callback_t callback,
    void* userdata);

/**
 * @brief Verify and repair a single file
 *
 * @param context Repair context
 * @param remote_name Remote file name
 * @param repair_mode Repair mode (verify only, auto repair, force)
 * @param stats Output repair statistics (can be NULL)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_file(
    netchunk_repair_context_t* context,
    const char* remote_name,
    netchunk_repair_mode_t repair_mode,
    netchunk_repair_stats_t* stats);

/**
 * @brief Verify and repair all files in the system
 *
 * @param context Repair context
 * @param repair_mode Repair mode (verify only, auto repair, force)
 * @param stats Output repair statistics (can be NULL)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_all_files(
    netchunk_repair_context_t* context,
    netchunk_repair_mode_t repair_mode,
    netchunk_repair_stats_t* stats);

/**
 * @brief Check health of a single chunk
 *
 * @param context Repair context
 * @param chunk Chunk to check
 * @param health Output chunk health status
 * @param healthy_replicas Output number of healthy replicas found
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_check_chunk_health(
    netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    netchunk_chunk_health_t* health,
    int* healthy_replicas);

/**
 * @brief Repair a single chunk by recreating missing replicas
 *
 * @param context Repair context
 * @param chunk Chunk to repair
 * @param target_replication Target replication factor
 * @param replicas_added Output number of replicas added
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_chunk(
    netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    int target_replication,
    int* replicas_added);

/**
 * @brief Remove corrupted replicas of a chunk
 *
 * @param context Repair context
 * @param chunk Chunk to clean up
 * @param replicas_removed Output number of replicas removed
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_cleanup_chunk(
    netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    int* replicas_removed);

/**
 * @brief Rebalance chunk distribution across servers
 *
 * This function attempts to evenly distribute chunks across all available
 * servers to improve performance and reliability.
 *
 * @param context Repair context
 * @param manifest File manifest to rebalance
 * @param moves_performed Output number of chunks moved
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_rebalance_chunks(
    netchunk_repair_context_t* context,
    netchunk_file_manifest_t* manifest,
    int* moves_performed);

/**
 * @brief Perform comprehensive system health check
 *
 * @param context Repair context
 * @param total_files Output total files in system
 * @param healthy_files Output files with no issues
 * @param degraded_files Output files with missing replicas
 * @param critical_files Output files with only one replica
 * @param lost_files Output files with no valid replicas
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_repair_system_health(
    netchunk_repair_context_t* context,
    uint32_t* total_files,
    uint32_t* healthy_files,
    uint32_t* degraded_files,
    uint32_t* critical_files,
    uint32_t* lost_files);

/**
 * @brief Cleanup repair engine context
 *
 * @param context Repair context to cleanup
 */
void netchunk_repair_cleanup(netchunk_repair_context_t* context);

#ifdef __cplusplus
}
#endif

#endif /* NETCHUNK_REPAIR_H */
