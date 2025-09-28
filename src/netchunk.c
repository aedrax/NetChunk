/**
 * @file netchunk.c
 * @brief NetChunk main API implementation
 *
 * Core integration layer that connects all NetChunk modules
 * and provides the main public API for file operations.
 */

#include "netchunk.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/**
 * @brief Internal helper to call progress callback safely
 */
static void call_progress_callback(netchunk_context_t* context,
    const char* operation,
    uint64_t current, uint64_t total,
    uint64_t bytes_current, uint64_t bytes_total)
{
    if (context->progress_cb) {
        context->progress_cb(context->progress_userdata, operation,
            current, total, bytes_current, bytes_total);
    }
}

/**
 * @brief Internal helper to get file size
 */
static int64_t get_file_size(const char* filepath)
{
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

netchunk_error_t netchunk_init(netchunk_context_t* context, const char* config_path)
{
    if (!context) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Initialize context to zero
    memset(context, 0, sizeof(netchunk_context_t));

    // Allocate and initialize configuration
    context->config = calloc(1, sizeof(netchunk_config_t));
    if (!context->config) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Load configuration
    netchunk_error_t error = netchunk_config_load(context->config, config_path);
    if (error != NETCHUNK_SUCCESS) {
        free(context->config);
        context->config = NULL;
        return error;
    }

    // Initialize FTP context
    context->ftp_context = calloc(1, sizeof(netchunk_ftp_context_t));
    if (!context->ftp_context) {
        netchunk_config_cleanup(context->config);
        free(context->config);
        context->config = NULL;
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    error = netchunk_ftp_init(context->ftp_context, context->config);
    if (error != NETCHUNK_SUCCESS) {
        free(context->ftp_context);
        context->ftp_context = NULL;
        netchunk_config_cleanup(context->config);
        free(context->config);
        context->config = NULL;
        return error;
    }

    context->initialized = true;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_set_progress_callback(netchunk_context_t* context,
    netchunk_progress_callback_t callback,
    void* userdata)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    context->progress_cb = callback;
    context->progress_userdata = userdata;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_upload(netchunk_context_t* context,
    const char* local_path,
    const char* remote_name,
    netchunk_stats_t* stats)
{
    if (!context || !context->initialized || !local_path || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_error_t error;
    netchunk_chunker_context_t chunker_ctx;
    netchunk_file_manifest_t manifest;
    time_t start_time = time(NULL);
    uint32_t retries = 0;

    // Initialize stats if provided
    if (stats) {
        memset(stats, 0, sizeof(netchunk_stats_t));
    }

    // Check if file exists and get size
    int64_t file_size = get_file_size(local_path);
    if (file_size < 0) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    call_progress_callback(context, "Preparing upload", 0, 1, 0, file_size);

    // Initialize chunker
    error = netchunk_chunker_init(&chunker_ctx, local_path, context->config->chunk_size);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    // Initialize manifest
    error = netchunk_manifest_init(&manifest, remote_name, file_size);
    if (error != NETCHUNK_SUCCESS) {
        netchunk_chunker_cleanup(&chunker_ctx);
        return error;
    }

    // Process chunks
    netchunk_chunk_t chunk;
    uint32_t chunk_number = 0;
    uint64_t bytes_processed = 0;

    call_progress_callback(context, "Uploading chunks", 0, chunker_ctx.total_chunks, 0, file_size);

    while ((error = netchunk_chunker_next_chunk(&chunker_ctx, &chunk)) == NETCHUNK_SUCCESS) {
        // Distribute chunk to servers with replication
        int successful_replicas = 0;
        int target_replicas = context->config->replication_factor;

        for (int server_idx = 0; server_idx < context->config->server_count && successful_replicas < target_replicas; server_idx++) {

            // Try to upload to this server
            for (int attempt = 0; attempt < context->config->max_retry_attempts; attempt++) {
                error = netchunk_ftp_upload_chunk(context->ftp_context,
                    &context->config->servers[server_idx],
                    &chunk);
                if (error == NETCHUNK_SUCCESS) {
                    // Add location to chunk
                    netchunk_chunk_location_t location;
                    strncpy(location.server_id, context->config->servers[server_idx].id,
                        sizeof(location.server_id) - 1);
                    location.server_id[sizeof(location.server_id) - 1] = '\0';
                    location.upload_time = time(NULL);

                    if (chunk.location_count < NETCHUNK_MAX_CHUNK_LOCATIONS) {
                        chunk.locations[chunk.location_count] = location;
                        chunk.location_count++;
                    }

                    successful_replicas++;
                    break;
                } else {
                    retries++;
                }
            }
        }

        // Check if we got enough replicas
        if (successful_replicas == 0) {
            netchunk_chunk_cleanup(&chunk);
            netchunk_manifest_cleanup(&manifest);
            netchunk_chunker_cleanup(&chunker_ctx);
            return NETCHUNK_ERROR_UPLOAD_FAILED;
        }

        // Add chunk to manifest
        error = netchunk_manifest_add_chunk(&manifest, &chunk);
        if (error != NETCHUNK_SUCCESS) {
            netchunk_chunk_cleanup(&chunk);
            netchunk_manifest_cleanup(&manifest);
            netchunk_chunker_cleanup(&chunker_ctx);
            return error;
        }

        bytes_processed += chunk.size;
        chunk_number++;

        call_progress_callback(context, "Uploading chunks", chunk_number,
            chunker_ctx.total_chunks, bytes_processed, file_size);

        netchunk_chunk_cleanup(&chunk);
    }

    // Check if chunking completed successfully (EOF expected)
    if (error != NETCHUNK_ERROR_EOF) {
        netchunk_manifest_cleanup(&manifest);
        netchunk_chunker_cleanup(&chunker_ctx);
        return error;
    }

    call_progress_callback(context, "Saving manifest", 1, 1, bytes_processed, file_size);

    // Upload manifest to servers
    error = netchunk_ftp_upload_manifest(context->ftp_context, context->config, &manifest);
    if (error != NETCHUNK_SUCCESS) {
        netchunk_manifest_cleanup(&manifest);
        netchunk_chunker_cleanup(&chunker_ctx);
        return error;
    }

    // Fill stats if provided
    if (stats) {
        stats->bytes_processed = bytes_processed;
        stats->chunks_processed = chunk_number;
        stats->servers_used = context->config->server_count;
        stats->elapsed_seconds = difftime(time(NULL), start_time);
        stats->retries_performed = retries;
    }

    call_progress_callback(context, "Upload complete", 1, 1, bytes_processed, file_size);

    netchunk_manifest_cleanup(&manifest);
    netchunk_chunker_cleanup(&chunker_ctx);
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_download(netchunk_context_t* context,
    const char* remote_name,
    const char* local_path,
    netchunk_stats_t* stats)
{
    if (!context || !context->initialized || !remote_name || !local_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_error_t error;
    netchunk_file_manifest_t manifest;
    time_t start_time = time(NULL);
    uint32_t retries = 0;

    // Initialize stats if provided
    if (stats) {
        memset(stats, 0, sizeof(netchunk_stats_t));
    }

    call_progress_callback(context, "Loading manifest", 0, 1, 0, 0);

    // Download manifest
    error = netchunk_ftp_download_manifest(context->ftp_context, context->config,
        remote_name, &manifest);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    call_progress_callback(context, "Downloading chunks", 0, manifest.chunk_count,
        0, manifest.original_size);

    // Open output file
    FILE* output_file = fopen(local_path, "wb");
    if (!output_file) {
        netchunk_manifest_cleanup(&manifest);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    // Download and write chunks in sequence
    uint64_t bytes_processed = 0;
    for (uint32_t i = 0; i < manifest.chunk_count; i++) {
        netchunk_chunk_t* chunk = &manifest.chunks[i];
        bool chunk_downloaded = false;

        // Try to download from any available location
        for (int loc_idx = 0; loc_idx < chunk->location_count && !chunk_downloaded; loc_idx++) {
            const char* server_id = chunk->locations[loc_idx].server_id;

            // Find server config
            netchunk_server_t* server = NULL;
            for (int s = 0; s < context->config->server_count; s++) {
                if (strcmp(context->config->servers[s].id, server_id) == 0) {
                    server = &context->config->servers[s];
                    break;
                }
            }

            if (!server)
                continue;

            // Try to download chunk with retries
            for (int attempt = 0; attempt < context->config->max_retry_attempts; attempt++) {
                error = netchunk_ftp_download_chunk(context->ftp_context, server, chunk);
                if (error == NETCHUNK_SUCCESS) {
                    // Verify chunk integrity
                    error = netchunk_chunk_verify_integrity(chunk);
                    if (error == NETCHUNK_SUCCESS) {
                        // Write chunk to file
                        size_t written = fwrite(chunk->data, 1, chunk->size, output_file);
                        if (written == chunk->size) {
                            chunk_downloaded = true;
                            bytes_processed += chunk->size;
                            break;
                        }
                    }
                } else {
                    retries++;
                }
            }
        }

        if (!chunk_downloaded) {
            fclose(output_file);
            remove(local_path);
            netchunk_manifest_cleanup(&manifest);
            return NETCHUNK_ERROR_DOWNLOAD_FAILED;
        }

        call_progress_callback(context, "Downloading chunks", i + 1, manifest.chunk_count,
            bytes_processed, manifest.original_size);
    }

    fclose(output_file);

    // Fill stats if provided
    if (stats) {
        stats->bytes_processed = bytes_processed;
        stats->chunks_processed = manifest.chunk_count;
        stats->servers_used = context->config->server_count;
        stats->elapsed_seconds = difftime(time(NULL), start_time);
        stats->retries_performed = retries;
    }

    call_progress_callback(context, "Download complete", 1, 1,
        manifest.original_size, manifest.original_size);

    netchunk_manifest_cleanup(&manifest);
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_list_files(netchunk_context_t* context,
    netchunk_file_manifest_t** files,
    size_t* count)
{
    if (!context || !context->initialized || !files || !count) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    return netchunk_ftp_list_manifests(context->ftp_context, context->config, files, count);
}

void netchunk_free_file_list(netchunk_file_manifest_t* files, size_t count)
{
    if (!files)
        return;

    for (size_t i = 0; i < count; i++) {
        netchunk_manifest_cleanup(&files[i]);
    }
    free(files);
}

netchunk_error_t netchunk_delete(netchunk_context_t* context, const char* remote_name)
{
    if (!context || !context->initialized || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_error_t error;
    netchunk_file_manifest_t manifest;

    // Download manifest first to get chunk locations
    error = netchunk_ftp_download_manifest(context->ftp_context, context->config,
        remote_name, &manifest);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    // Delete all chunks from all servers
    for (uint32_t i = 0; i < manifest.chunk_count; i++) {
        netchunk_chunk_t* chunk = &manifest.chunks[i];

        for (int loc_idx = 0; loc_idx < chunk->location_count; loc_idx++) {
            const char* server_id = chunk->locations[loc_idx].server_id;

            // Find server config
            netchunk_server_t* server = NULL;
            for (int s = 0; s < context->config->server_count; s++) {
                if (strcmp(context->config->servers[s].id, server_id) == 0) {
                    server = &context->config->servers[s];
                    break;
                }
            }

            if (server) {
                netchunk_ftp_delete_chunk(context->ftp_context, server, chunk);
            }
        }
    }

    // Delete manifest from all servers
    error = netchunk_ftp_delete_manifest(context->ftp_context, context->config, remote_name);

    netchunk_manifest_cleanup(&manifest);
    return error;
}

netchunk_error_t netchunk_verify(netchunk_context_t* context,
    const char* remote_name,
    bool repair,
    uint32_t* chunks_verified,
    uint32_t* chunks_repaired)
{
    if (!context || !context->initialized || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Initialize output parameters
    if (chunks_verified)
        *chunks_verified = 0;
    if (chunks_repaired)
        *chunks_repaired = 0;

    netchunk_error_t error;
    netchunk_file_manifest_t manifest;
    uint32_t verified_count = 0;
    uint32_t repaired_count = 0;

    // Download manifest
    error = netchunk_ftp_download_manifest(context->ftp_context, context->config,
        remote_name, &manifest);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    call_progress_callback(context, "Verifying chunks", 0, manifest.chunk_count, 0, 0);

    // Verify each chunk
    for (uint32_t i = 0; i < manifest.chunk_count; i++) {
        netchunk_chunk_t* chunk = &manifest.chunks[i];
        bool chunk_ok = false;
        int healthy_replicas = 0;

        // Check each replica
        for (int loc_idx = 0; loc_idx < chunk->location_count; loc_idx++) {
            const char* server_id = chunk->locations[loc_idx].server_id;

            // Find server config
            netchunk_server_t* server = NULL;
            for (int s = 0; s < context->config->server_count; s++) {
                if (strcmp(context->config->servers[s].id, server_id) == 0) {
                    server = &context->config->servers[s];
                    break;
                }
            }

            if (!server)
                continue;

            // Download and verify chunk
            netchunk_error_t download_error = netchunk_ftp_download_chunk(
                context->ftp_context, server, chunk);
            if (download_error == NETCHUNK_SUCCESS) {
                netchunk_error_t verify_error = netchunk_chunk_verify_integrity(chunk);
                if (verify_error == NETCHUNK_SUCCESS) {
                    healthy_replicas++;
                    chunk_ok = true;
                }
            }
        }

        verified_count++;

        // If repair is enabled and chunk needs repair
        if (repair && (healthy_replicas < context->config->replication_factor) && chunk_ok) {
            // Re-replicate chunk to meet replication factor
            // This is a simplified repair - a full repair engine would be more sophisticated
            int target_replicas = context->config->replication_factor - healthy_replicas;

            for (int server_idx = 0; server_idx < context->config->server_count && target_replicas > 0; server_idx++) {

                // Skip servers that already have this chunk
                bool server_has_chunk = false;
                for (int loc_idx = 0; loc_idx < chunk->location_count; loc_idx++) {
                    if (strcmp(chunk->locations[loc_idx].server_id,
                            context->config->servers[server_idx].id)
                        == 0) {
                        server_has_chunk = true;
                        break;
                    }
                }

                if (server_has_chunk)
                    continue;

                // Upload chunk to this server
                error = netchunk_ftp_upload_chunk(context->ftp_context,
                    &context->config->servers[server_idx],
                    chunk);
                if (error == NETCHUNK_SUCCESS) {
                    target_replicas--;
                    repaired_count++;
                }
            }
        }

        call_progress_callback(context, "Verifying chunks", i + 1, manifest.chunk_count, 0, 0);
    }

    if (chunks_verified)
        *chunks_verified = verified_count;
    if (chunks_repaired)
        *chunks_repaired = repaired_count;

    netchunk_manifest_cleanup(&manifest);
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_health_check(netchunk_context_t* context,
    uint32_t* healthy_servers,
    uint32_t* total_servers)
{
    if (!context || !context->initialized) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    uint32_t healthy_count = 0;
    uint32_t total_count = context->config->server_count;

    for (int i = 0; i < context->config->server_count; i++) {
        netchunk_error_t error = netchunk_ftp_test_connection(
            context->ftp_context, &context->config->servers[i]);
        if (error == NETCHUNK_SUCCESS) {
            healthy_count++;
        }
    }

    if (healthy_servers)
        *healthy_servers = healthy_count;
    if (total_servers)
        *total_servers = total_count;

    return NETCHUNK_SUCCESS;
}

void netchunk_get_version(int* major, int* minor, int* patch, const char** version_string)
{
    if (major)
        *major = NETCHUNK_VERSION_MAJOR;
    if (minor)
        *minor = NETCHUNK_VERSION_MINOR;
    if (patch)
        *patch = NETCHUNK_VERSION_PATCH;
    if (version_string)
        *version_string = NETCHUNK_VERSION_STRING;
}

void netchunk_cleanup(netchunk_context_t* context)
{
    if (!context)
        return;

    if (context->ftp_context) {
        netchunk_ftp_cleanup(context->ftp_context);
        free(context->ftp_context);
        context->ftp_context = NULL;
    }

    if (context->config) {
        netchunk_config_cleanup(context->config);
        free(context->config);
        context->config = NULL;
    }

    context->progress_cb = NULL;
    context->progress_userdata = NULL;
    context->initialized = false;
}
