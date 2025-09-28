/**
 * @file repair.c
 * @brief NetChunk repair engine implementation
 *
 * Automatic chunk repair and replication management implementation.
 * Handles integrity verification, missing chunk detection, and
 * automatic recovery from available replicas.
 */

#include "repair.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Internal helper to call progress callback safely
 */
static void call_repair_progress_callback(netchunk_repair_context_t* context,
    uint32_t current_chunk,
    uint32_t total_chunks,
    const netchunk_repair_stats_t* stats)
{
    if (context->progress_cb) {
        context->progress_cb(context->progress_userdata, current_chunk, total_chunks, stats);
    }
}

/**
 * @brief Find server configuration by ID
 */
static netchunk_server_t* find_server_by_id(netchunk_config_t* config, const char* server_id)
{
    for (int i = 0; i < config->server_count; i++) {
        if (strcmp(config->servers[i].id, server_id) == 0) {
            return &config->servers[i];
        }
    }
    return NULL;
}

/**
 * @brief Select best server for new chunk replica
 */
static netchunk_server_t* select_server_for_replica(netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk)
{
    int server_usage[NETCHUNK_MAX_SERVERS] = { 0 };

    // Count existing replicas per server
    for (int i = 0; i < chunk->location_count; i++) {
        for (int s = 0; s < context->config->server_count; s++) {
            if (strcmp(chunk->locations[i].server_id, context->config->servers[s].id) == 0) {
                server_usage[s]++;
                break;
            }
        }
    }

    // Find server with least usage that doesn't already have this chunk
    int best_server_idx = -1;
    int min_usage = INT_MAX;

    for (int s = 0; s < context->config->server_count; s++) {
        if (server_usage[s] == 0 && server_usage[s] < min_usage) {
            // Test if server is healthy
            netchunk_error_t test_result = netchunk_ftp_test_connection(
                context->ftp_context, &context->config->servers[s]);
            if (test_result == NETCHUNK_SUCCESS) {
                min_usage = server_usage[s];
                best_server_idx = s;
            }
        }
    }

    return (best_server_idx >= 0) ? &context->config->servers[best_server_idx] : NULL;
}

netchunk_error_t netchunk_repair_init(netchunk_repair_context_t* context,
    netchunk_config_t* config,
    netchunk_ftp_context_t* ftp_context)
{
    if (!context || !config || !ftp_context) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(netchunk_repair_context_t));
    context->config = config;
    context->ftp_context = ftp_context;
    context->repair_mode = NETCHUNK_REPAIR_AUTO;
    context->initialized = true;

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_set_progress_callback(netchunk_repair_context_t* context,
    netchunk_repair_progress_callback_t callback,
    void* userdata)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    context->progress_cb = callback;
    context->progress_userdata = userdata;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_check_chunk_health(netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    netchunk_chunk_health_t* health,
    int* healthy_replicas)
{
    if (!context || !context->initialized || !chunk || !health || !healthy_replicas) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    int healthy_count = 0;
    int total_locations = chunk->location_count;

    // Check each replica
    for (int i = 0; i < chunk->location_count; i++) {
        netchunk_server_t* server = find_server_by_id(context->config,
            chunk->locations[i].server_id);
        if (!server) {
            continue;
        }

        // Try to download and verify chunk
        netchunk_chunk_t temp_chunk = *chunk;
        netchunk_error_t download_result = netchunk_ftp_download_chunk(
            context->ftp_context, server, &temp_chunk);

        if (download_result == NETCHUNK_SUCCESS) {
            netchunk_error_t verify_result = netchunk_chunk_verify_integrity(&temp_chunk);
            if (verify_result == NETCHUNK_SUCCESS) {
                healthy_count++;
            }
            // Clean up temporary chunk data
            if (temp_chunk.data && temp_chunk.data != chunk->data) {
                free(temp_chunk.data);
                temp_chunk.data = NULL;
            }
        }
    }

    *healthy_replicas = healthy_count;

    // Determine health status
    if (healthy_count == 0) {
        *health = NETCHUNK_CHUNK_LOST;
    } else if (healthy_count == 1) {
        *health = NETCHUNK_CHUNK_CRITICAL;
    } else if (healthy_count < context->config->replication_factor) {
        *health = NETCHUNK_CHUNK_DEGRADED;
    } else {
        *health = NETCHUNK_CHUNK_HEALTHY;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_cleanup_chunk(netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    int* replicas_removed)
{
    if (!context || !context->initialized || !chunk || !replicas_removed) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    *replicas_removed = 0;
    int valid_locations = 0;
    netchunk_chunk_location_t valid_location_list[NETCHUNK_MAX_CHUNK_LOCATIONS];

    // Check each replica and keep only valid ones
    for (int i = 0; i < chunk->location_count; i++) {
        netchunk_server_t* server = find_server_by_id(context->config,
            chunk->locations[i].server_id);
        if (!server) {
            (*replicas_removed)++;
            continue;
        }

        // Try to verify replica
        netchunk_chunk_t temp_chunk = *chunk;
        netchunk_error_t download_result = netchunk_ftp_download_chunk(
            context->ftp_context, server, &temp_chunk);

        bool replica_valid = false;
        if (download_result == NETCHUNK_SUCCESS) {
            netchunk_error_t verify_result = netchunk_chunk_verify_integrity(&temp_chunk);
            if (verify_result == NETCHUNK_SUCCESS) {
                replica_valid = true;
            }
            // Clean up temporary chunk data
            if (temp_chunk.data && temp_chunk.data != chunk->data) {
                free(temp_chunk.data);
                temp_chunk.data = NULL;
            }
        }

        if (replica_valid) {
            // Keep this location
            valid_location_list[valid_locations] = chunk->locations[i];
            valid_locations++;
        } else {
            // Remove corrupted replica from server
            netchunk_ftp_delete_chunk(context->ftp_context, server, chunk);
            (*replicas_removed)++;
        }
    }

    // Update chunk with valid locations only
    chunk->location_count = valid_locations;
    for (int i = 0; i < valid_locations; i++) {
        chunk->locations[i] = valid_location_list[i];
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_chunk(netchunk_repair_context_t* context,
    netchunk_chunk_t* chunk,
    int target_replication,
    int* replicas_added)
{
    if (!context || !context->initialized || !chunk || !replicas_added) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    *replicas_added = 0;

    // First, ensure we have valid chunk data
    netchunk_chunk_t working_chunk = *chunk;
    bool have_valid_data = false;

    // Try to get valid chunk data from existing replicas
    for (int i = 0; i < chunk->location_count && !have_valid_data; i++) {
        netchunk_server_t* server = find_server_by_id(context->config,
            chunk->locations[i].server_id);
        if (!server)
            continue;

        netchunk_error_t download_result = netchunk_ftp_download_chunk(
            context->ftp_context, server, &working_chunk);

        if (download_result == NETCHUNK_SUCCESS) {
            netchunk_error_t verify_result = netchunk_chunk_verify_integrity(&working_chunk);
            if (verify_result == NETCHUNK_SUCCESS) {
                have_valid_data = true;
            } else {
                // Clean up bad data
                if (working_chunk.data && working_chunk.data != chunk->data) {
                    free(working_chunk.data);
                    working_chunk.data = NULL;
                }
            }
        }
    }

    if (!have_valid_data) {
        return NETCHUNK_ERROR_CHUNK_INTEGRITY;
    }

    // Now create additional replicas as needed
    int replicas_needed = target_replication - chunk->location_count;

    for (int i = 0; i < replicas_needed; i++) {
        netchunk_server_t* target_server = select_server_for_replica(context, chunk);
        if (!target_server) {
            break; // No more suitable servers available
        }

        // Upload chunk to selected server
        netchunk_error_t upload_result = netchunk_ftp_upload_chunk(
            context->ftp_context, target_server, &working_chunk);

        if (upload_result == NETCHUNK_SUCCESS) {
            // Add new location to chunk
            if (chunk->location_count < NETCHUNK_MAX_CHUNK_LOCATIONS) {
                netchunk_chunk_location_t new_location;
                strncpy(new_location.server_id, target_server->id,
                    sizeof(new_location.server_id) - 1);
                new_location.server_id[sizeof(new_location.server_id) - 1] = '\0';
                new_location.upload_time = time(NULL);

                chunk->locations[chunk->location_count] = new_location;
                chunk->location_count++;
                (*replicas_added)++;
            }
        }
    }

    // Clean up working chunk data if it was allocated
    if (working_chunk.data && working_chunk.data != chunk->data) {
        free(working_chunk.data);
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_file(netchunk_repair_context_t* context,
    const char* remote_name,
    netchunk_repair_mode_t repair_mode,
    netchunk_repair_stats_t* stats)
{
    if (!context || !context->initialized || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_repair_stats_t local_stats = { 0 };
    time_t start_time = time(NULL);
    netchunk_error_t error;
    netchunk_file_manifest_t manifest;

    // Download manifest
    error = netchunk_ftp_download_manifest(context->ftp_context, context->config,
        remote_name, &manifest);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    local_stats.chunks_verified = manifest.chunk_count;

    // Process each chunk
    for (uint32_t i = 0; i < manifest.chunk_count; i++) {
        netchunk_chunk_t* chunk = &manifest.chunks[i];
        netchunk_chunk_health_t health;
        int healthy_replicas;

        call_repair_progress_callback(context, i, manifest.chunk_count, &local_stats);

        // Check chunk health
        error = netchunk_repair_check_chunk_health(context, chunk, &health, &healthy_replicas);
        if (error != NETCHUNK_SUCCESS) {
            continue; // Skip problematic chunks
        }

        // Update statistics based on health
        switch (health) {
        case NETCHUNK_CHUNK_HEALTHY:
            local_stats.chunks_healthy++;
            break;
        case NETCHUNK_CHUNK_DEGRADED:
            local_stats.chunks_degraded++;
            break;
        case NETCHUNK_CHUNK_CRITICAL:
            local_stats.chunks_critical++;
            break;
        case NETCHUNK_CHUNK_LOST:
            local_stats.chunks_lost++;
            break;
        }

        // Perform repair if needed and enabled
        if (repair_mode != NETCHUNK_REPAIR_VERIFY_ONLY && health != NETCHUNK_CHUNK_HEALTHY) {
            // Clean up corrupted replicas first
            int replicas_removed;
            netchunk_repair_cleanup_chunk(context, chunk, &replicas_removed);
            local_stats.replicas_removed += replicas_removed;

            // Add missing replicas if we have valid data
            if (health != NETCHUNK_CHUNK_LOST) {
                int replicas_added;
                error = netchunk_repair_chunk(context, chunk, context->config->replication_factor,
                    &replicas_added);
                if (error == NETCHUNK_SUCCESS && replicas_added > 0) {
                    local_stats.replicas_added += replicas_added;
                    local_stats.chunks_repaired++;
                }
            }
        }
    }

    // Update manifest with any location changes if repairs were made
    if (repair_mode != NETCHUNK_REPAIR_VERIFY_ONLY && local_stats.chunks_repaired > 0) {
        netchunk_ftp_upload_manifest(context->ftp_context, context->config, &manifest);
    }

    local_stats.elapsed_seconds = difftime(time(NULL), start_time);

    call_repair_progress_callback(context, manifest.chunk_count, manifest.chunk_count, &local_stats);

    if (stats) {
        *stats = local_stats;
    }

    netchunk_manifest_cleanup(&manifest);
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_all_files(netchunk_repair_context_t* context,
    netchunk_repair_mode_t repair_mode,
    netchunk_repair_stats_t* stats)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_file_manifest_t* files;
    size_t file_count;
    netchunk_repair_stats_t total_stats = { 0 };
    time_t start_time = time(NULL);

    // Get list of all files
    netchunk_error_t error = netchunk_ftp_list_manifests(context->ftp_context,
        context->config, &files, &file_count);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    // Repair each file
    for (size_t i = 0; i < file_count; i++) {
        netchunk_repair_stats_t file_stats;

        error = netchunk_repair_file(context, files[i].original_filename, repair_mode, &file_stats);
        if (error == NETCHUNK_SUCCESS) {
            // Aggregate statistics
            total_stats.chunks_verified += file_stats.chunks_verified;
            total_stats.chunks_healthy += file_stats.chunks_healthy;
            total_stats.chunks_degraded += file_stats.chunks_degraded;
            total_stats.chunks_critical += file_stats.chunks_critical;
            total_stats.chunks_lost += file_stats.chunks_lost;
            total_stats.chunks_repaired += file_stats.chunks_repaired;
            total_stats.replicas_added += file_stats.replicas_added;
            total_stats.replicas_removed += file_stats.replicas_removed;
        }
    }

    total_stats.elapsed_seconds = difftime(time(NULL), start_time);

    if (stats) {
        *stats = total_stats;
    }

    netchunk_free_file_list(files, file_count);
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_rebalance_chunks(netchunk_repair_context_t* context,
    netchunk_file_manifest_t* manifest,
    int* moves_performed)
{
    if (!context || !context->initialized || !manifest || !moves_performed) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    *moves_performed = 0;

    // Count chunks per server
    int server_chunk_count[NETCHUNK_MAX_SERVERS] = { 0 };
    int total_chunks = 0;

    for (uint32_t i = 0; i < manifest->chunk_count; i++) {
        netchunk_chunk_t* chunk = &manifest->chunks[i];
        for (int j = 0; j < chunk->location_count; j++) {
            for (int s = 0; s < context->config->server_count; s++) {
                if (strcmp(chunk->locations[j].server_id,
                        context->config->servers[s].id)
                    == 0) {
                    server_chunk_count[s]++;
                    total_chunks++;
                    break;
                }
            }
        }
    }

    if (total_chunks == 0) {
        return NETCHUNK_SUCCESS;
    }

    // Calculate target distribution
    int avg_chunks_per_server = total_chunks / context->config->server_count;
    int remainder = total_chunks % context->config->server_count;

    // Find overloaded and underloaded servers
    for (int from_server = 0; from_server < context->config->server_count; from_server++) {
        int target_count = avg_chunks_per_server + (from_server < remainder ? 1 : 0);

        while (server_chunk_count[from_server] > target_count) {
            // Find an underloaded server
            int to_server = -1;
            for (int s = 0; s < context->config->server_count; s++) {
                int s_target = avg_chunks_per_server + (s < remainder ? 1 : 0);
                if (server_chunk_count[s] < s_target) {
                    to_server = s;
                    break;
                }
            }

            if (to_server < 0)
                break; // No underloaded servers found

            // Find a chunk to move from from_server
            bool moved = false;
            for (uint32_t i = 0; i < manifest->chunk_count && !moved; i++) {
                netchunk_chunk_t* chunk = &manifest->chunks[i];

                // Check if this chunk is on from_server and not on to_server
                bool on_from_server = false;
                bool on_to_server = false;
                int from_location_idx = -1;

                for (int j = 0; j < chunk->location_count; j++) {
                    if (strcmp(chunk->locations[j].server_id,
                            context->config->servers[from_server].id)
                        == 0) {
                        on_from_server = true;
                        from_location_idx = j;
                    }
                    if (strcmp(chunk->locations[j].server_id,
                            context->config->servers[to_server].id)
                        == 0) {
                        on_to_server = true;
                    }
                }

                if (on_from_server && !on_to_server && chunk->location_count < NETCHUNK_MAX_CHUNK_LOCATIONS) {
                    // Move this chunk replica
                    netchunk_error_t upload_result = netchunk_ftp_upload_chunk(
                        context->ftp_context, &context->config->servers[to_server], chunk);

                    if (upload_result == NETCHUNK_SUCCESS) {
                        // Add new location
                        netchunk_chunk_location_t new_location;
                        strncpy(new_location.server_id, context->config->servers[to_server].id,
                            sizeof(new_location.server_id) - 1);
                        new_location.server_id[sizeof(new_location.server_id) - 1] = '\0';
                        new_location.upload_time = time(NULL);

                        chunk->locations[chunk->location_count] = new_location;
                        chunk->location_count++;

                        // Remove from from_server if we have enough replicas
                        if (chunk->location_count > context->config->replication_factor) {
                            netchunk_ftp_delete_chunk(context->ftp_context,
                                &context->config->servers[from_server], chunk);

                            // Remove location from array
                            for (int k = from_location_idx; k < chunk->location_count - 1; k++) {
                                chunk->locations[k] = chunk->locations[k + 1];
                            }
                            chunk->location_count--;
                        }

                        server_chunk_count[from_server]--;
                        server_chunk_count[to_server]++;
                        (*moves_performed)++;
                        moved = true;
                    }
                }
            }

            if (!moved)
                break; // Couldn't find any chunks to move
        }
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_repair_system_health(netchunk_repair_context_t* context,
    uint32_t* total_files,
    uint32_t* healthy_files,
    uint32_t* degraded_files,
    uint32_t* critical_files,
    uint32_t* lost_files)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_file_manifest_t* files;
    size_t file_count;
    uint32_t healthy = 0, degraded = 0, critical = 0, lost = 0;

    // Get list of all files
    netchunk_error_t error = netchunk_ftp_list_manifests(context->ftp_context,
        context->config, &files, &file_count);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    // Check health of each file
    for (size_t i = 0; i < file_count; i++) {
        bool file_has_lost_chunks = false;
        bool file_has_critical_chunks = false;
        bool file_has_degraded_chunks = false;

        // Download full manifest for detailed chunk information
        netchunk_file_manifest_t full_manifest;
        error = netchunk_ftp_download_manifest(context->ftp_context, context->config,
            files[i].original_filename, &full_manifest);
        if (error != NETCHUNK_SUCCESS) {
            continue; // Skip files we can't analyze
        }

        // Check each chunk
        for (uint32_t j = 0; j < full_manifest.chunk_count; j++) {
            netchunk_chunk_health_t chunk_health;
            int healthy_replicas;

            error = netchunk_repair_check_chunk_health(context, &full_manifest.chunks[j],
                &chunk_health, &healthy_replicas);
            if (error == NETCHUNK_SUCCESS) {
                switch (chunk_health) {
                case NETCHUNK_CHUNK_LOST:
                    file_has_lost_chunks = true;
                    break;
                case NETCHUNK_CHUNK_CRITICAL:
                    file_has_critical_chunks = true;
                    break;
                case NETCHUNK_CHUNK_DEGRADED:
                    file_has_degraded_chunks = true;
                    break;
                default:
                    break;
                }
            }
        }

        // Categorize file based on worst chunk condition
        if (file_has_lost_chunks) {
            lost++;
        } else if (file_has_critical_chunks) {
            critical++;
        } else if (file_has_degraded_chunks) {
            degraded++;
        } else {
            healthy++;
        }

        netchunk_manifest_cleanup(&full_manifest);
    }

    // Set output parameters
    if (total_files)
        *total_files = file_count;
    if (healthy_files)
        *healthy_files = healthy;
    if (degraded_files)
        *degraded_files = degraded;
    if (critical_files)
        *critical_files = critical;
    if (lost_files)
        *lost_files = lost;

    netchunk_free_file_list(files, file_count);
    return NETCHUNK_SUCCESS;
}

void netchunk_repair_cleanup(netchunk_repair_context_t* context)
{
    if (!context)
        return;

    context->config = NULL;
    context->ftp_context = NULL;
    context->progress_cb = NULL;
    context->progress_userdata = NULL;
    context->initialized = false;
}
