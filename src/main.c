/**
 * @file main.c
 * @brief NetChunk CLI application
 *
 * Command-line interface for the NetChunk distributed file storage system.
 * Provides user-friendly access to all NetChunk operations with progress
 * feedback and error handling.
 */

#include "netchunk.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/**
 * @brief CLI command types
 */
typedef enum {
    CMD_UPLOAD,
    CMD_DOWNLOAD,
    CMD_LIST,
    CMD_DELETE,
    CMD_VERIFY,
    CMD_HEALTH,
    CMD_VERSION,
    CMD_HELP,
    CMD_UNKNOWN
} netchunk_command_t;

/**
 * @brief CLI configuration
 */
typedef struct {
    netchunk_command_t command;
    char* config_path;
    char* local_path;
    char* remote_name;
    bool repair;
    bool verbose;
    bool quiet;
    bool show_stats;
} cli_config_t;

/**
 * @brief Progress callback context for CLI
 */
typedef struct {
    bool verbose;
    time_t last_update;
    char current_operation[256];
} progress_context_t;

/**
 * @brief Print CLI usage information
 */
static void print_usage(const char* program_name)
{
    printf("NetChunk %s - Distributed File Storage System\n\n", NETCHUNK_VERSION_STRING);
    printf("USAGE:\n");
    printf("  %s [OPTIONS] COMMAND [ARGS...]\n\n", program_name);

    printf("COMMANDS:\n");
    printf("  upload <local_file> <remote_name>    Upload a file to distributed storage\n");
    printf("  download <remote_name> <local_file>  Download a file from distributed storage\n");
    printf("  list                                 List all files in distributed storage\n");
    printf("  delete <remote_name>                 Delete a file from distributed storage\n");
    printf("  verify <remote_name> [--repair]      Verify file integrity, optionally repair\n");
    printf("  health                               Check health of all configured servers\n");
    printf("  version                              Show version information\n");
    printf("  help                                 Show this help message\n\n");

    printf("OPTIONS:\n");
    printf("  -c, --config PATH                    Path to configuration file\n");
    printf("  -v, --verbose                        Enable verbose output\n");
    printf("  -q, --quiet                          Suppress progress output\n");
    printf("  -s, --stats                          Show operation statistics\n");
    printf("  -r, --repair                         Enable repair mode for verify command\n");
    printf("  -h, --help                           Show this help message\n\n");

    printf("EXAMPLES:\n");
    printf("  %s upload /path/to/file.txt myfile.txt\n", program_name);
    printf("  %s download myfile.txt /path/to/downloaded.txt\n", program_name);
    printf("  %s list\n", program_name);
    printf("  %s verify myfile.txt --repair\n", program_name);
    printf("  %s health\n", program_name);
    printf("\nFor more information, visit: https://github.com/aedrax/NetChunk\n");
}

/**
 * @brief Format bytes in human-readable form
 */
static void format_bytes(uint64_t bytes, char* buffer, size_t buffer_size)
{
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit = 0;
    double size = (double)bytes;

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }

    if (unit == 0) {
        snprintf(buffer, buffer_size, "%llu %s", (unsigned long long)bytes, units[unit]);
    } else {
        snprintf(buffer, buffer_size, "%.1f %s", size, units[unit]);
    }
}

/**
 * @brief Format time duration in human-readable form
 */
static void format_duration(double seconds, char* buffer, size_t buffer_size)
{
    if (seconds < 60) {
        snprintf(buffer, buffer_size, "%.1fs", seconds);
    } else if (seconds < 3600) {
        snprintf(buffer, buffer_size, "%.1fm", seconds / 60);
    } else {
        snprintf(buffer, buffer_size, "%.1fh", seconds / 3600);
    }
}

/**
 * @brief Progress callback implementation for CLI
 */
static void progress_callback(void* userdata, const char* operation_name,
    uint64_t current, uint64_t total,
    uint64_t bytes_current, uint64_t bytes_total)
{
    progress_context_t* ctx = (progress_context_t*)userdata;
    time_t now = time(NULL);

    // Update operation name if changed
    if (strcmp(ctx->current_operation, operation_name) != 0) {
        strncpy(ctx->current_operation, operation_name, sizeof(ctx->current_operation) - 1);
        ctx->current_operation[sizeof(ctx->current_operation) - 1] = '\0';

        if (ctx->verbose) {
            printf("\n%s...\n", operation_name);
        }
    }

    // Rate limit progress updates to avoid spam
    if (now == ctx->last_update && current != total) {
        return;
    }
    ctx->last_update = now;

    if (ctx->verbose && total > 0) {
        double percent = (double)current / total * 100.0;
        char bytes_str[32] = "";
        char total_str[32] = "";

        if (bytes_total > 0) {
            format_bytes(bytes_current, bytes_str, sizeof(bytes_str));
            format_bytes(bytes_total, total_str, sizeof(total_str));
            printf("\rProgress: %.1f%% (%llu/%llu) - %s / %s",
                percent, (unsigned long long)current, (unsigned long long)total,
                bytes_str, total_str);
        } else {
            printf("\rProgress: %.1f%% (%llu/%llu)",
                percent, (unsigned long long)current, (unsigned long long)total);
        }
        fflush(stdout);

        if (current == total) {
            printf("\n");
        }
    }
}

/**
 * @brief Print operation statistics
 */
static void print_stats(const netchunk_stats_t* stats)
{
    char bytes_str[32], duration_str[32];
    format_bytes(stats->bytes_processed, bytes_str, sizeof(bytes_str));
    format_duration(stats->elapsed_seconds, duration_str, sizeof(duration_str));

    printf("\nOperation Statistics:\n");
    printf("  Bytes processed:  %s\n", bytes_str);
    printf("  Chunks processed: %u\n", stats->chunks_processed);
    printf("  Servers used:     %u\n", stats->servers_used);
    printf("  Duration:         %s\n", duration_str);
    printf("  Retries:          %u\n", stats->retries_performed);

    if (stats->elapsed_seconds > 0) {
        double rate_mbps = (stats->bytes_processed / 1024.0 / 1024.0) / stats->elapsed_seconds;
        printf("  Transfer rate:    %.1f MB/s\n", rate_mbps);
    }
}

/**
 * @brief Parse command line arguments
 */
static int parse_arguments(int argc, char* argv[], cli_config_t* config)
{
    // Initialize config
    memset(config, 0, sizeof(cli_config_t));
    config->command = CMD_UNKNOWN;

    static struct option long_options[] = {
        { "config", required_argument, 0, 'c' },
        { "verbose", no_argument, 0, 'v' },
        { "quiet", no_argument, 0, 'q' },
        { "stats", no_argument, 0, 's' },
        { "repair", no_argument, 0, 'r' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "c:vqsrh", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'c':
            config->config_path = strdup(optarg);
            break;
        case 'v':
            config->verbose = true;
            break;
        case 'q':
            config->quiet = true;
            break;
        case 's':
            config->show_stats = true;
            break;
        case 'r':
            config->repair = true;
            break;
        case 'h':
            config->command = CMD_HELP;
            return 0;
        default:
            return -1;
        }
    }

    // Parse command
    if (optind >= argc) {
        return -1;
    }

    const char* command_str = argv[optind];
    if (strcmp(command_str, "upload") == 0) {
        config->command = CMD_UPLOAD;
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: upload command requires <local_file> and <remote_name>\n");
            return -1;
        }
        config->local_path = strdup(argv[optind + 1]);
        config->remote_name = strdup(argv[optind + 2]);
    } else if (strcmp(command_str, "download") == 0) {
        config->command = CMD_DOWNLOAD;
        if (optind + 2 >= argc) {
            fprintf(stderr, "Error: download command requires <remote_name> and <local_file>\n");
            return -1;
        }
        config->remote_name = strdup(argv[optind + 1]);
        config->local_path = strdup(argv[optind + 2]);
    } else if (strcmp(command_str, "list") == 0) {
        config->command = CMD_LIST;
    } else if (strcmp(command_str, "delete") == 0) {
        config->command = CMD_DELETE;
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: delete command requires <remote_name>\n");
            return -1;
        }
        config->remote_name = strdup(argv[optind + 1]);
    } else if (strcmp(command_str, "verify") == 0) {
        config->command = CMD_VERIFY;
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: verify command requires <remote_name>\n");
            return -1;
        }
        config->remote_name = strdup(argv[optind + 1]);
    } else if (strcmp(command_str, "health") == 0) {
        config->command = CMD_HEALTH;
    } else if (strcmp(command_str, "version") == 0) {
        config->command = CMD_VERSION;
    } else if (strcmp(command_str, "help") == 0) {
        config->command = CMD_HELP;
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command_str);
        return -1;
    }

    return 0;
}

/**
 * @brief Clean up CLI configuration
 */
static void cleanup_config(cli_config_t* config)
{
    free(config->config_path);
    free(config->local_path);
    free(config->remote_name);
}

/**
 * @brief Get error message string
 */
static const char* get_error_message(netchunk_error_t error)
{
    switch (error) {
    case NETCHUNK_SUCCESS:
        return "Success";
    case NETCHUNK_ERROR_INVALID_ARGUMENT:
        return "Invalid argument";
    case NETCHUNK_ERROR_OUT_OF_MEMORY:
        return "Out of memory";
    case NETCHUNK_ERROR_FILE_NOT_FOUND:
        return "File not found";
    case NETCHUNK_ERROR_FILE_ACCESS:
        return "File access error";
    case NETCHUNK_ERROR_NETWORK:
        return "Network error";
    case NETCHUNK_ERROR_CONFIG:
        return "Configuration error";
    case NETCHUNK_ERROR_UPLOAD_FAILED:
        return "Upload failed";
    case NETCHUNK_ERROR_DOWNLOAD_FAILED:
        return "Download failed";
    case NETCHUNK_ERROR_CHUNK_INTEGRITY:
        return "Chunk integrity error";
    default:
        return "Unknown error";
    }
}

/**
 * @brief Main application entry point
 */
int main(int argc, char* argv[])
{
    cli_config_t config;
    netchunk_context_t netchunk_ctx;
    progress_context_t progress_ctx = { 0 };
    netchunk_error_t error;
    int exit_code = 0;

    // Parse command line arguments
    if (parse_arguments(argc, argv, &config) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    // Handle help and version commands immediately
    if (config.command == CMD_HELP) {
        print_usage(argv[0]);
        cleanup_config(&config);
        return 0;
    }

    if (config.command == CMD_VERSION) {
        int major, minor, patch;
        const char* version_str;
        netchunk_get_version(&major, &minor, &patch, &version_str);
        printf("NetChunk version %s\n", version_str);
        printf("Build date: %s %s\n", __DATE__, __TIME__);
        cleanup_config(&config);
        return 0;
    }

    // Initialize NetChunk context
    error = netchunk_init(&netchunk_ctx, config.config_path);
    if (error != NETCHUNK_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize NetChunk: %s\n", get_error_message(error));
        if (error == NETCHUNK_ERROR_CONFIG) {
            fprintf(stderr, "Please check your configuration file.\n");
        }
        cleanup_config(&config);
        return 1;
    }

    // Set up progress callback if not quiet
    if (!config.quiet) {
        progress_ctx.verbose = config.verbose;
        netchunk_set_progress_callback(&netchunk_ctx, progress_callback, &progress_ctx);
    }

    // Execute command
    netchunk_stats_t stats;

    switch (config.command) {
    case CMD_UPLOAD: {
        if (config.verbose) {
            printf("Uploading '%s' as '%s'...\n", config.local_path, config.remote_name);
        }

        error = netchunk_upload(&netchunk_ctx, config.local_path, config.remote_name, &stats);
        if (error == NETCHUNK_SUCCESS) {
            if (!config.quiet) {
                printf("Upload completed successfully.\n");
            }
            if (config.show_stats) {
                print_stats(&stats);
            }
        } else {
            fprintf(stderr, "Error: Upload failed: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    case CMD_DOWNLOAD: {
        if (config.verbose) {
            printf("Downloading '%s' to '%s'...\n", config.remote_name, config.local_path);
        }

        error = netchunk_download(&netchunk_ctx, config.remote_name, config.local_path, &stats);
        if (error == NETCHUNK_SUCCESS) {
            if (!config.quiet) {
                printf("Download completed successfully.\n");
            }
            if (config.show_stats) {
                print_stats(&stats);
            }
        } else {
            fprintf(stderr, "Error: Download failed: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    case CMD_LIST: {
        netchunk_file_manifest_t* files;
        size_t file_count;

        error = netchunk_list_files(&netchunk_ctx, &files, &file_count);
        if (error == NETCHUNK_SUCCESS) {
            if (file_count == 0) {
                printf("No files found in distributed storage.\n");
            } else {
                printf("Files in distributed storage:\n\n");
                printf("%-30s %12s %12s %s\n", "Name", "Size", "Chunks", "Upload Time");
                printf("%-30s %12s %12s %s\n", "----", "----", "------", "-----------");

                for (size_t i = 0; i < file_count; i++) {
                    char size_str[32];
                    format_bytes(files[i].original_size, size_str, sizeof(size_str));

                    char time_str[64];
                    struct tm* tm_info = localtime(&files[i].created_timestamp);
                    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

                    printf("%-30s %12s %12u %s\n",
                        files[i].original_filename, size_str, files[i].chunk_count, time_str);
                }

                printf("\nTotal: %zu files\n", file_count);
            }

            netchunk_free_file_list(files, file_count);
        } else {
            fprintf(stderr, "Error: Failed to list files: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    case CMD_DELETE: {
        if (config.verbose) {
            printf("Deleting '%s'...\n", config.remote_name);
        }

        error = netchunk_delete(&netchunk_ctx, config.remote_name);
        if (error == NETCHUNK_SUCCESS) {
            if (!config.quiet) {
                printf("File deleted successfully.\n");
            }
        } else {
            fprintf(stderr, "Error: Delete failed: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    case CMD_VERIFY: {
        uint32_t chunks_verified, chunks_repaired;

        if (config.verbose) {
            printf("Verifying '%s'%s...\n", config.remote_name,
                config.repair ? " (repair mode)" : "");
        }

        error = netchunk_verify(&netchunk_ctx, config.remote_name, config.repair,
            &chunks_verified, &chunks_repaired);
        if (error == NETCHUNK_SUCCESS) {
            if (!config.quiet) {
                printf("Verification completed: %u chunks verified", chunks_verified);
                if (config.repair && chunks_repaired > 0) {
                    printf(", %u chunks repaired", chunks_repaired);
                }
                printf(".\n");
            }
        } else {
            fprintf(stderr, "Error: Verification failed: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    case CMD_HEALTH: {
        uint32_t healthy_servers, total_servers;

        if (config.verbose) {
            printf("Checking server health...\n");
        }

        error = netchunk_health_check(&netchunk_ctx, &healthy_servers, &total_servers);
        if (error == NETCHUNK_SUCCESS) {
            printf("Server Health Status:\n");
            printf("  Healthy servers: %u / %u\n", healthy_servers, total_servers);

            if (healthy_servers == total_servers) {
                printf("  Status: All servers healthy ✓\n");
            } else if (healthy_servers == 0) {
                printf("  Status: All servers offline ✗\n");
                exit_code = 1;
            } else {
                printf("  Status: Partial connectivity ⚠\n");
                exit_code = 1;
            }
        } else {
            fprintf(stderr, "Error: Health check failed: %s\n", get_error_message(error));
            exit_code = 1;
        }
        break;
    }

    default:
        fprintf(stderr, "Error: Unknown command\n");
        print_usage(argv[0]);
        exit_code = 1;
        break;
    }

    // Cleanup
    netchunk_cleanup(&netchunk_ctx);
    cleanup_config(&config);

    return exit_code;
}
