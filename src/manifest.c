#include "manifest.h"
#include "crypto.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Internal helper functions
static netchunk_error_t write_file_atomically(const char* file_path, const char* content);
static netchunk_error_t read_file_content(const char* file_path, char** content);
static int compare_timestamps(const void* a, const void* b);
static netchunk_error_t ensure_directory_exists(const char* dir_path);

// Manifest Management Functions

netchunk_error_t netchunk_manifest_manager_init(netchunk_manifest_manager_t* manager,
    const char* manifest_directory,
    netchunk_config_t* config)
{
    if (!manager || !manifest_directory || !config) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(manager, 0, sizeof(netchunk_manifest_manager_t));

    // Expand directory path
    netchunk_error_t path_error = netchunk_config_expand_path(manifest_directory,
        manager->manifest_directory,
        sizeof(manager->manifest_directory));
    if (path_error != NETCHUNK_SUCCESS) {
        return path_error;
    }

    manager->config = config;
    manager->auto_backup = true;
    manager->max_backups = 5;

    // Ensure directory exists
    return netchunk_manifest_ensure_directory(manager);
}

void netchunk_manifest_manager_cleanup(netchunk_manifest_manager_t* manager)
{
    if (!manager) {
        return;
    }

    memset(manager, 0, sizeof(netchunk_manifest_manager_t));
}

netchunk_error_t netchunk_manifest_ensure_directory(netchunk_manifest_manager_t* manager)
{
    if (!manager) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    return ensure_directory_exists(manager->manifest_directory);
}

// File Manifest Functions

netchunk_error_t netchunk_file_manifest_init(netchunk_file_manifest_t* manifest,
    const netchunk_file_info_t* file_info,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count)
{
    if (!manifest || !file_info) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(manifest, 0, sizeof(netchunk_file_manifest_t));

    // Copy basic file information
    strncpy(manifest->original_filename, file_info->filename, NETCHUNK_MAX_PATH_LEN - 1);
    strcpy(manifest->version, NETCHUNK_MANIFEST_VERSION);
    manifest->total_size = file_info->total_size;
    memcpy(manifest->file_hash, file_info->file_hash, NETCHUNK_HASH_LENGTH);
    manifest->chunk_size = file_info->chunk_size;
    manifest->chunk_count = file_info->chunk_count;
    manifest->created_timestamp = file_info->created_timestamp;
    manifest->last_accessed = file_info->last_accessed;
    manifest->last_modified = file_info->created_timestamp;
    manifest->last_verified = 0;

    // Generate manifest ID
    netchunk_error_t id_error = netchunk_generate_manifest_id(manifest->manifest_id,
        file_info->filename,
        file_info->file_hash);
    if (id_error != NETCHUNK_SUCCESS) {
        return id_error;
    }

    // Set up chunks if provided
    if (chunks && chunk_count > 0) {
        manifest->chunks = malloc(sizeof(netchunk_chunk_t) * chunk_count);
        if (!manifest->chunks) {
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }

        memcpy(manifest->chunks, chunks, sizeof(netchunk_chunk_t) * chunk_count);
        manifest->chunks_owned = true;
    } else {
        manifest->chunks = NULL;
        manifest->chunks_owned = false;
    }

    // Set default replication settings
    manifest->replication_factor = NETCHUNK_DEFAULT_REPLICATION_FACTOR;
    manifest->min_replicas_required = 1;

    // Set creator info
    snprintf(manifest->creator_info, sizeof(manifest->creator_info),
        "NetChunk v%s", NETCHUNK_MANIFEST_VERSION);

    return NETCHUNK_SUCCESS;
}

void netchunk_file_manifest_cleanup(netchunk_file_manifest_t* manifest)
{
    if (!manifest) {
        return;
    }

    if (manifest->chunks && manifest->chunks_owned) {
        // Cleanup individual chunks
        for (uint32_t i = 0; i < manifest->chunk_count; i++) {
            netchunk_chunk_cleanup(&manifest->chunks[i]);
        }
        free(manifest->chunks);
        manifest->chunks = NULL;
    }

    manifest->chunks_owned = false;
}

netchunk_error_t netchunk_file_manifest_create_from_chunker(netchunk_file_manifest_t* manifest,
    const netchunk_chunker_context_t* chunker_context,
    netchunk_chunk_t* chunks,
    uint32_t chunk_count)
{
    if (!manifest || !chunker_context) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    return netchunk_file_manifest_init(manifest, &chunker_context->file_info, chunks, chunk_count);
}

netchunk_error_t netchunk_generate_manifest_id(char* manifest_id,
    const char* filename,
    const uint8_t* file_hash)
{
    if (!manifest_id || !filename || !file_hash) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Generate random bytes for uniqueness
    uint8_t random_bytes[16];
    netchunk_error_t random_error = netchunk_generate_random_bytes(random_bytes, sizeof(random_bytes));
    if (random_error != NETCHUNK_SUCCESS) {
        return random_error;
    }

    // Create ID from filename hash prefix and random bytes
    snprintf(manifest_id, 64, "manifest_%02x%02x%02x%02x_%02x%02x%02x%02x_%02x%02x%02x%02x_%02x%02x%02x%02x",
        file_hash[0], file_hash[1], file_hash[2], file_hash[3],
        random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
        random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
        random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11]);

    return NETCHUNK_SUCCESS;
}

// JSON Serialization Functions

netchunk_error_t netchunk_file_manifest_to_json(const netchunk_file_manifest_t* manifest,
    char** json_output)
{
    if (!manifest || !json_output) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Basic information
    cJSON_AddStringToObject(root, "version", manifest->version);
    cJSON_AddStringToObject(root, "manifest_id", manifest->manifest_id);
    cJSON_AddStringToObject(root, "original_filename", manifest->original_filename);
    cJSON_AddNumberToObject(root, "total_size", (double)manifest->total_size);
    cJSON_AddNumberToObject(root, "chunk_size", (double)manifest->chunk_size);
    cJSON_AddNumberToObject(root, "chunk_count", manifest->chunk_count);

    // File hash as hex string
    char hash_hex[NETCHUNK_HASH_LENGTH * 2 + 1];
    netchunk_hash_to_hex_string(manifest->file_hash, NETCHUNK_HASH_LENGTH, hash_hex);
    cJSON_AddStringToObject(root, "file_hash", hash_hex);

    // Timestamps
    cJSON_AddNumberToObject(root, "created_timestamp", (double)manifest->created_timestamp);
    cJSON_AddNumberToObject(root, "last_accessed", (double)manifest->last_accessed);
    cJSON_AddNumberToObject(root, "last_modified", (double)manifest->last_modified);
    cJSON_AddNumberToObject(root, "last_verified", (double)manifest->last_verified);

    // Replication settings
    cJSON_AddNumberToObject(root, "replication_factor", manifest->replication_factor);
    cJSON_AddNumberToObject(root, "min_replicas_required", manifest->min_replicas_required);

    // Metadata
    cJSON_AddStringToObject(root, "creator_info", manifest->creator_info);
    cJSON_AddStringToObject(root, "comment", manifest->comment);

    // Chunks array
    cJSON* chunks_array = cJSON_CreateArray();
    if (!chunks_array) {
        cJSON_Delete(root);
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < manifest->chunk_count; i++) {
        if (manifest->chunks) {
            cJSON* chunk_json = netchunk_chunk_to_json(&manifest->chunks[i]);
            if (chunk_json) {
                cJSON_AddItemToArray(chunks_array, chunk_json);
            }
        }
    }

    cJSON_AddItemToObject(root, "chunks", chunks_array);

    // Convert to string
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_string) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    *json_output = json_string;
    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_file_manifest_from_json(const char* json_input,
    netchunk_file_manifest_t* manifest)
{
    if (!json_input || !manifest) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(manifest, 0, sizeof(netchunk_file_manifest_t));

    cJSON* root = cJSON_Parse(json_input);
    if (!root) {
        return NETCHUNK_ERROR_MANIFEST_CORRUPT;
    }

    // Basic information
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsString(version)) {
        strncpy(manifest->version, version->valuestring, sizeof(manifest->version) - 1);
    }

    cJSON* manifest_id = cJSON_GetObjectItem(root, "manifest_id");
    if (manifest_id && cJSON_IsString(manifest_id)) {
        strncpy(manifest->manifest_id, manifest_id->valuestring, sizeof(manifest->manifest_id) - 1);
    }

    cJSON* filename = cJSON_GetObjectItem(root, "original_filename");
    if (filename && cJSON_IsString(filename)) {
        strncpy(manifest->original_filename, filename->valuestring, NETCHUNK_MAX_PATH_LEN - 1);
    }

    cJSON* total_size = cJSON_GetObjectItem(root, "total_size");
    if (total_size && cJSON_IsNumber(total_size)) {
        manifest->total_size = (size_t)total_size->valuedouble;
    }

    cJSON* chunk_size = cJSON_GetObjectItem(root, "chunk_size");
    if (chunk_size && cJSON_IsNumber(chunk_size)) {
        manifest->chunk_size = (size_t)chunk_size->valuedouble;
    }

    cJSON* chunk_count = cJSON_GetObjectItem(root, "chunk_count");
    if (chunk_count && cJSON_IsNumber(chunk_count)) {
        manifest->chunk_count = (uint32_t)chunk_count->valuedouble;
    }

    // File hash from hex string
    cJSON* file_hash_hex = cJSON_GetObjectItem(root, "file_hash");
    if (file_hash_hex && cJSON_IsString(file_hash_hex)) {
        netchunk_hex_string_to_hash(file_hash_hex->valuestring,
            manifest->file_hash,
            NETCHUNK_HASH_LENGTH);
    }

    // Timestamps
    cJSON* created = cJSON_GetObjectItem(root, "created_timestamp");
    if (created && cJSON_IsNumber(created)) {
        manifest->created_timestamp = (time_t)created->valuedouble;
    }

    cJSON* accessed = cJSON_GetObjectItem(root, "last_accessed");
    if (accessed && cJSON_IsNumber(accessed)) {
        manifest->last_accessed = (time_t)accessed->valuedouble;
    }

    cJSON* modified = cJSON_GetObjectItem(root, "last_modified");
    if (modified && cJSON_IsNumber(modified)) {
        manifest->last_modified = (time_t)modified->valuedouble;
    }

    cJSON* verified = cJSON_GetObjectItem(root, "last_verified");
    if (verified && cJSON_IsNumber(verified)) {
        manifest->last_verified = (time_t)verified->valuedouble;
    }

    // Replication settings
    cJSON* replication_factor = cJSON_GetObjectItem(root, "replication_factor");
    if (replication_factor && cJSON_IsNumber(replication_factor)) {
        manifest->replication_factor = (int)replication_factor->valuedouble;
    }

    cJSON* min_replicas = cJSON_GetObjectItem(root, "min_replicas_required");
    if (min_replicas && cJSON_IsNumber(min_replicas)) {
        manifest->min_replicas_required = (int)min_replicas->valuedouble;
    }

    // Metadata
    cJSON* creator_info = cJSON_GetObjectItem(root, "creator_info");
    if (creator_info && cJSON_IsString(creator_info)) {
        strncpy(manifest->creator_info, creator_info->valuestring, sizeof(manifest->creator_info) - 1);
    }

    cJSON* comment = cJSON_GetObjectItem(root, "comment");
    if (comment && cJSON_IsString(comment)) {
        strncpy(manifest->comment, comment->valuestring, sizeof(manifest->comment) - 1);
    }

    // Chunks array
    cJSON* chunks_array = cJSON_GetObjectItem(root, "chunks");
    if (chunks_array && cJSON_IsArray(chunks_array)) {
        int array_size = cJSON_GetArraySize(chunks_array);
        if (array_size > 0) {
            manifest->chunks = malloc(sizeof(netchunk_chunk_t) * array_size);
            if (!manifest->chunks) {
                cJSON_Delete(root);
                return NETCHUNK_ERROR_OUT_OF_MEMORY;
            }

            manifest->chunks_owned = true;
            int chunks_loaded = 0;

            for (int i = 0; i < array_size; i++) {
                cJSON* chunk_json = cJSON_GetArrayItem(chunks_array, i);
                if (chunk_json) {
                    netchunk_error_t chunk_error = netchunk_chunk_from_json(chunk_json, &manifest->chunks[chunks_loaded]);
                    if (chunk_error == NETCHUNK_SUCCESS) {
                        chunks_loaded++;
                    }
                }
            }

            manifest->chunk_count = chunks_loaded;
        }
    }

    cJSON_Delete(root);
    return NETCHUNK_SUCCESS;
}

cJSON* netchunk_chunk_to_json(const netchunk_chunk_t* chunk)
{
    if (!chunk) {
        return NULL;
    }

    cJSON* chunk_json = cJSON_CreateObject();
    if (!chunk_json) {
        return NULL;
    }

    // Basic chunk information
    cJSON_AddStringToObject(chunk_json, "id", chunk->id);
    cJSON_AddNumberToObject(chunk_json, "sequence_number", chunk->sequence_number);
    cJSON_AddNumberToObject(chunk_json, "size", (double)chunk->size);
    cJSON_AddNumberToObject(chunk_json, "created_timestamp", (double)chunk->created_timestamp);

    // Hash as hex string
    char hash_hex[NETCHUNK_HASH_LENGTH * 2 + 1];
    netchunk_hash_to_hex_string(chunk->hash, NETCHUNK_HASH_LENGTH, hash_hex);
    cJSON_AddStringToObject(chunk_json, "hash", hash_hex);

    // Locations array
    cJSON* locations_array = cJSON_CreateArray();
    if (locations_array) {
        for (int i = 0; i < chunk->location_count; i++) {
            const netchunk_chunk_location_t* location = &chunk->locations[i];

            cJSON* location_json = cJSON_CreateObject();
            if (location_json) {
                cJSON_AddStringToObject(location_json, "server_id", location->server_id);
                cJSON_AddStringToObject(location_json, "remote_path", location->remote_path);
                cJSON_AddNumberToObject(location_json, "upload_time", (double)location->upload_time);
                cJSON_AddBoolToObject(location_json, "verified", location->verified);
                cJSON_AddNumberToObject(location_json, "last_verified", (double)location->last_verified);

                cJSON_AddItemToArray(locations_array, location_json);
            }
        }

        cJSON_AddItemToObject(chunk_json, "locations", locations_array);
    }

    return chunk_json;
}

netchunk_error_t netchunk_chunk_from_json(const cJSON* json, netchunk_chunk_t* chunk)
{
    if (!json || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(chunk, 0, sizeof(netchunk_chunk_t));

    // Basic chunk information
    cJSON* id = cJSON_GetObjectItem(json, "id");
    if (id && cJSON_IsString(id)) {
        strncpy(chunk->id, id->valuestring, sizeof(chunk->id) - 1);
    }

    cJSON* sequence = cJSON_GetObjectItem(json, "sequence_number");
    if (sequence && cJSON_IsNumber(sequence)) {
        chunk->sequence_number = (uint32_t)sequence->valuedouble;
    }

    cJSON* size = cJSON_GetObjectItem(json, "size");
    if (size && cJSON_IsNumber(size)) {
        chunk->size = (size_t)size->valuedouble;
    }

    cJSON* created = cJSON_GetObjectItem(json, "created_timestamp");
    if (created && cJSON_IsNumber(created)) {
        chunk->created_timestamp = (time_t)created->valuedouble;
    }

    // Hash from hex string
    cJSON* hash_hex = cJSON_GetObjectItem(json, "hash");
    if (hash_hex && cJSON_IsString(hash_hex)) {
        netchunk_hex_string_to_hash(hash_hex->valuestring, chunk->hash, NETCHUNK_HASH_LENGTH);
    }

    // Locations array
    cJSON* locations_array = cJSON_GetObjectItem(json, "locations");
    if (locations_array && cJSON_IsArray(locations_array)) {
        int array_size = cJSON_GetArraySize(locations_array);
        chunk->location_count = 0;

        for (int i = 0; i < array_size && i < NETCHUNK_MAX_CHUNK_LOCATIONS; i++) {
            cJSON* location_json = cJSON_GetArrayItem(locations_array, i);
            if (location_json) {
                netchunk_chunk_location_t* location = &chunk->locations[chunk->location_count];

                cJSON* server_id = cJSON_GetObjectItem(location_json, "server_id");
                if (server_id && cJSON_IsString(server_id)) {
                    strncpy(location->server_id, server_id->valuestring, sizeof(location->server_id) - 1);
                }

                cJSON* remote_path = cJSON_GetObjectItem(location_json, "remote_path");
                if (remote_path && cJSON_IsString(remote_path)) {
                    strncpy(location->remote_path, remote_path->valuestring, NETCHUNK_MAX_PATH_LEN - 1);
                }

                cJSON* uploaded = cJSON_GetObjectItem(location_json, "upload_time");
                if (uploaded && cJSON_IsNumber(uploaded)) {
                    location->upload_time = (time_t)uploaded->valuedouble;
                }

                cJSON* verified = cJSON_GetObjectItem(location_json, "verified");
                if (verified && cJSON_IsBool(verified)) {
                    location->verified = cJSON_IsTrue(verified);
                }

                cJSON* last_verified = cJSON_GetObjectItem(location_json, "last_verified");
                if (last_verified && cJSON_IsNumber(last_verified)) {
                    location->last_verified = (time_t)last_verified->valuedouble;
                }

                chunk->location_count++;
            }
        }
    }

    chunk->data = NULL;
    chunk->data_owned = false;

    return NETCHUNK_SUCCESS;
}

// File Operations

netchunk_error_t netchunk_manifest_save_to_file(netchunk_manifest_manager_t* manager,
    const netchunk_file_manifest_t* manifest,
    const char* filename)
{
    if (!manager || !manifest || !filename) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Build full path
    char full_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    netchunk_error_t path_error = netchunk_manifest_build_path(manager, filename, full_path, sizeof(full_path));
    if (path_error != NETCHUNK_SUCCESS) {
        return path_error;
    }

    // Create backup if auto-backup is enabled and file exists
    if (manager->auto_backup && access(full_path, F_OK) == 0) {
        netchunk_manifest_backup(manager, filename);
    }

    // Convert manifest to JSON
    char* json_content;
    netchunk_error_t json_error = netchunk_file_manifest_to_json(manifest, &json_content);
    if (json_error != NETCHUNK_SUCCESS) {
        return json_error;
    }

    // Write file atomically
    netchunk_error_t write_error = write_file_atomically(full_path, json_content);

    // Cleanup JSON string
    free(json_content);

    return write_error;
}

netchunk_error_t netchunk_manifest_load_from_file(netchunk_manifest_manager_t* manager,
    const char* filename,
    netchunk_file_manifest_t* manifest)
{
    if (!manager || !filename || !manifest) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Build full path
    char full_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    netchunk_error_t path_error = netchunk_manifest_build_path(manager, filename, full_path, sizeof(full_path));
    if (path_error != NETCHUNK_SUCCESS) {
        return path_error;
    }

    // Read file content
    char* json_content;
    netchunk_error_t read_error = read_file_content(full_path, &json_content);
    if (read_error != NETCHUNK_SUCCESS) {
        return read_error;
    }

    // Parse JSON
    netchunk_error_t parse_error = netchunk_file_manifest_from_json(json_content, manifest);

    // Cleanup
    free(json_content);

    return parse_error;
}

netchunk_error_t netchunk_manifest_delete_file(netchunk_manifest_manager_t* manager,
    const char* filename)
{
    if (!manager || !filename) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Build full path
    char full_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    netchunk_error_t path_error = netchunk_manifest_build_path(manager, filename, full_path, sizeof(full_path));
    if (path_error != NETCHUNK_SUCCESS) {
        return path_error;
    }

    // Create backup before deletion if enabled
    if (manager->auto_backup) {
        netchunk_manifest_backup(manager, filename);
    }

    // Delete file
    if (unlink(full_path) != 0) {
        if (errno == ENOENT) {
            return NETCHUNK_ERROR_FILE_NOT_FOUND;
        } else {
            return NETCHUNK_ERROR_FILE_ACCESS;
        }
    }

    return NETCHUNK_SUCCESS;
}

// Utility Functions

netchunk_error_t netchunk_manifest_build_path(netchunk_manifest_manager_t* manager,
    const char* filename,
    char* full_path,
    size_t path_len)
{
    if (!manager || !filename || !full_path || path_len == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    int result = snprintf(full_path, path_len, "%s/%s%s",
        manager->manifest_directory,
        filename,
        NETCHUNK_MANIFEST_EXTENSION);

    if (result >= (int)path_len || result < 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    return NETCHUNK_SUCCESS;
}

bool netchunk_manifest_file_exists(netchunk_manifest_manager_t* manager,
    const char* filename)
{
    if (!manager || !filename) {
        return false;
    }

    char full_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    netchunk_error_t path_error = netchunk_manifest_build_path(manager, filename, full_path, sizeof(full_path));
    if (path_error != NETCHUNK_SUCCESS) {
        return false;
    }

    return access(full_path, F_OK) == 0;
}

void netchunk_manifest_update_timestamps(netchunk_file_manifest_t* manifest,
    bool update_accessed,
    bool update_modified,
    bool update_verified)
{
    if (!manifest) {
        return;
    }

    time_t current_time = time(NULL);

    if (update_accessed) {
        manifest->last_accessed = current_time;
    }

    if (update_modified) {
        manifest->last_modified = current_time;
    }

    if (update_verified) {
        manifest->last_verified = current_time;
    }
}

// Validation Functions

netchunk_error_t netchunk_manifest_validate(const netchunk_file_manifest_t* manifest)
{
    if (!manifest) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Check version
    if (strlen(manifest->version) == 0) {
        return NETCHUNK_ERROR_MANIFEST_CORRUPT;
    }

    // Check basic fields
    if (strlen(manifest->original_filename) == 0 || strlen(manifest->manifest_id) == 0 || manifest->total_size == 0 || manifest->chunk_size == 0 || manifest->chunk_count == 0) {
        return NETCHUNK_ERROR_MANIFEST_CORRUPT;
    }

    // Validate chunk size
    if (manifest->chunk_size < NETCHUNK_MIN_CHUNK_SIZE || manifest->chunk_size > NETCHUNK_MAX_CHUNK_SIZE) {
        return NETCHUNK_ERROR_CONFIG_VALIDATION;
    }

    // Check chunk count consistency
    uint32_t expected_chunks = netchunk_calculate_chunk_count(manifest->total_size, manifest->chunk_size);
    if (manifest->chunk_count != expected_chunks) {
        return NETCHUNK_ERROR_MANIFEST_CORRUPT;
    }

    // Validate chunks if present
    if (manifest->chunks) {
        for (uint32_t i = 0; i < manifest->chunk_count; i++) {
            const netchunk_chunk_t* chunk = &manifest->chunks[i];

            if (strlen(chunk->id) == 0 || chunk->sequence_number != i) {
                return NETCHUNK_ERROR_MANIFEST_CORRUPT;
            }

            if (chunk->location_count < 0 || chunk->location_count > NETCHUNK_MAX_CHUNK_LOCATIONS) {
                return NETCHUNK_ERROR_MANIFEST_CORRUPT;
            }
        }
    }

    return NETCHUNK_SUCCESS;
}

// Simple manifest functions (aliases and simplified versions)

netchunk_error_t netchunk_manifest_init(netchunk_file_manifest_t* manifest,
    const char* remote_name,
    size_t file_size)
{
    if (!manifest || !remote_name) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    memset(manifest, 0, sizeof(netchunk_file_manifest_t));

    // Set basic information
    strncpy(manifest->original_filename, remote_name, NETCHUNK_MAX_PATH_LEN - 1);
    strcpy(manifest->version, NETCHUNK_MANIFEST_VERSION);
    manifest->total_size = file_size;
    manifest->original_size = file_size; // Set both fields
    manifest->created_timestamp = time(NULL);
    manifest->last_accessed = time(NULL);
    manifest->last_modified = time(NULL);

    // Generate a simple manifest ID
    snprintf(manifest->manifest_id, sizeof(manifest->manifest_id),
        "manifest_%s_%ld", remote_name, time(NULL));

    // Set default replication
    manifest->replication_factor = NETCHUNK_DEFAULT_REPLICATION_FACTOR;
    manifest->min_replicas_required = 1;

    // Set creator info
    snprintf(manifest->creator_info, sizeof(manifest->creator_info),
        "NetChunk v%s", NETCHUNK_MANIFEST_VERSION);

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_manifest_add_chunk(netchunk_file_manifest_t* manifest,
    const netchunk_chunk_t* chunk)
{
    if (!manifest || !chunk) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Allocate or expand chunks array
    if (!manifest->chunks) {
        manifest->chunks = malloc(sizeof(netchunk_chunk_t));
        if (!manifest->chunks) {
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }
        manifest->chunks_owned = true;
        manifest->chunk_count = 0;
    } else {
        // Reallocate for one more chunk
        netchunk_chunk_t* new_chunks = realloc(manifest->chunks,
            sizeof(netchunk_chunk_t) * (manifest->chunk_count + 1));
        if (!new_chunks) {
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }
        manifest->chunks = new_chunks;
    }

    // Copy chunk data
    manifest->chunks[manifest->chunk_count] = *chunk;

    // If chunk owns data, we need to copy it
    if (chunk->data_owned && chunk->data) {
        manifest->chunks[manifest->chunk_count].data = malloc(chunk->size);
        if (!manifest->chunks[manifest->chunk_count].data) {
            return NETCHUNK_ERROR_OUT_OF_MEMORY;
        }
        memcpy(manifest->chunks[manifest->chunk_count].data, chunk->data, chunk->size);
        manifest->chunks[manifest->chunk_count].data_owned = true;
    }

    manifest->chunk_count++;
    return NETCHUNK_SUCCESS;
}

void netchunk_manifest_cleanup(netchunk_file_manifest_t* manifest)
{
    netchunk_file_manifest_cleanup(manifest);
}

// Internal helper functions

static netchunk_error_t write_file_atomically(const char* file_path, const char* content)
{
    if (!file_path || !content) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Create temporary file name
    char temp_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    snprintf(temp_path, sizeof(temp_path), "%s%s", file_path, NETCHUNK_MANIFEST_TEMP_SUFFIX);

    // Write to temporary file
    FILE* temp_file = fopen(temp_path, "w");
    if (!temp_file) {
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    if (fprintf(temp_file, "%s", content) < 0) {
        fclose(temp_file);
        unlink(temp_path);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    if (fflush(temp_file) != 0) {
        fclose(temp_file);
        unlink(temp_path);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    fclose(temp_file);

    // Atomically move temporary file to final location
    if (rename(temp_path, file_path) != 0) {
        unlink(temp_path);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    return NETCHUNK_SUCCESS;
}

static netchunk_error_t read_file_content(const char* file_path, char** content)
{
    if (!file_path || !content) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    FILE* file = fopen(file_path, "r");
    if (!file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size < 0) {
        fclose(file);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    // Allocate buffer
    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Read file content
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    buffer[file_size] = '\0';
    *content = buffer;

    return NETCHUNK_SUCCESS;
}

static int compare_timestamps(const void* a, const void* b)
{
    time_t time_a = *(const time_t*)a;
    time_t time_b = *(const time_t*)b;

    if (time_a < time_b)
        return -1;
    if (time_a > time_b)
        return 1;
    return 0;
}

static netchunk_error_t ensure_directory_exists(const char* dir_path)
{
    if (!dir_path) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    struct stat st;

    // Check if directory already exists
    if (stat(dir_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return NETCHUNK_SUCCESS;
        } else {
            return NETCHUNK_ERROR_FILE_ACCESS; // Path exists but is not a directory
        }
    }

    // Create directory (with parent directories if needed)
    char path_copy[NETCHUNK_MAX_PATH_LEN];
    strncpy(path_copy, dir_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* p = path_copy + 1; // Skip leading slash if present

    while (*p) {
        if (*p == '/') {
            *p = '\0';

            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                return NETCHUNK_ERROR_FILE_ACCESS;
            }

            *p = '/';
        }
        p++;
    }

    // Create final directory
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    return NETCHUNK_SUCCESS;
}

// Additional stub implementations for backup functions

netchunk_error_t netchunk_manifest_backup(netchunk_manifest_manager_t* manager,
    const char* filename)
{
    if (!manager || !filename) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Build source path
    char source_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    netchunk_error_t path_error = netchunk_manifest_build_path(manager, filename, source_path, sizeof(source_path));
    if (path_error != NETCHUNK_SUCCESS) {
        return path_error;
    }

    // Check if source file exists
    if (access(source_path, F_OK) != 0) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    // Build backup path with timestamp
    char backup_path[NETCHUNK_MAX_MANIFEST_PATH_LEN];
    time_t current_time = time(NULL);
    snprintf(backup_path, sizeof(backup_path), "%s.backup.%ld", source_path, current_time);

    // Read source file
    char* content;
    netchunk_error_t read_error = read_file_content(source_path, &content);
    if (read_error != NETCHUNK_SUCCESS) {
        return read_error;
    }

    // Write backup file
    netchunk_error_t write_error = write_file_atomically(backup_path, content);
    free(content);

    return write_error;
}

void netchunk_manifest_get_statistics(const netchunk_file_manifest_t* manifest,
    uint32_t* total_chunks_stored,
    size_t* total_chunk_size,
    double* avg_replicas,
    int* min_replicas,
    uint32_t* missing_chunks)
{
    if (!manifest) {
        return;
    }

    uint32_t stored_count = 0;
    size_t size_sum = 0;
    int replica_sum = 0;
    int min_reps = INT_MAX;
    uint32_t missing_count = 0;

    for (uint32_t i = 0; i < manifest->chunk_count; i++) {
        const netchunk_chunk_t* chunk = &manifest->chunks[i];

        if (chunk->location_count > 0) {
            stored_count++;
            size_sum += chunk->size;
            replica_sum += chunk->location_count;

            if (chunk->location_count < min_reps) {
                min_reps = chunk->location_count;
            }
        } else {
            missing_count++;
        }
    }

    if (total_chunks_stored) {
        *total_chunks_stored = stored_count;
    }

    if (total_chunk_size) {
        *total_chunk_size = size_sum;
    }

    if (avg_replicas) {
        *avg_replicas = stored_count > 0 ? (double)replica_sum / stored_count : 0.0;
    }

    if (min_replicas) {
        *min_replicas = stored_count > 0 ? min_reps : 0;
    }

    if (missing_chunks) {
        *missing_chunks = missing_count;
    }
}

netchunk_error_t netchunk_manifest_find_under_replicated_chunks(const netchunk_file_manifest_t* manifest,
    int min_replicas,
    uint32_t** chunk_indices,
    size_t* count)
{
    if (!manifest || !chunk_indices || !count || min_replicas <= 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Count under-replicated chunks
    size_t under_replicated_count = 0;
    for (uint32_t i = 0; i < manifest->chunk_count; i++) {
        if (manifest->chunks[i].location_count < min_replicas) {
            under_replicated_count++;
        }
    }

    if (under_replicated_count == 0) {
        *chunk_indices = NULL;
        *count = 0;
        return NETCHUNK_SUCCESS;
    }

    // Allocate array for indices
    uint32_t* indices = malloc(sizeof(uint32_t) * under_replicated_count);
    if (!indices) {
        return NETCHUNK_ERROR_OUT_OF_MEMORY;
    }

    // Fill indices array
    size_t index = 0;
    for (uint32_t i = 0; i < manifest->chunk_count; i++) {
        if (manifest->chunks[i].location_count < min_replicas) {
            indices[index++] = i;
        }
    }

    *chunk_indices = indices;
    *count = under_replicated_count;

    return NETCHUNK_SUCCESS;
}
