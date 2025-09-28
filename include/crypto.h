#ifndef NETCHUNK_CRYPTO_H
#define NETCHUNK_CRYPTO_H

#include "config.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cryptographic constants
#define NETCHUNK_SHA256_DIGEST_LENGTH 32
#define NETCHUNK_SHA256_BLOCK_SIZE 64

// Hash context for streaming operations
typedef struct netchunk_sha256_context {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[NETCHUNK_SHA256_BLOCK_SIZE];
} netchunk_sha256_context_t;

// Hash Functions

/**
 * @brief Initialize SHA-256 context
 * @param context Context to initialize
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_sha256_init(netchunk_sha256_context_t* context);

/**
 * @brief Update SHA-256 hash with data
 * @param context SHA-256 context
 * @param data Data to hash
 * @param data_len Length of data
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_sha256_update(netchunk_sha256_context_t* context,
    const uint8_t* data,
    size_t data_len);

/**
 * @brief Finalize SHA-256 hash and get result
 * @param context SHA-256 context
 * @param hash Output buffer for hash (must be at least NETCHUNK_SHA256_DIGEST_LENGTH bytes)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_sha256_final(netchunk_sha256_context_t* context,
    uint8_t* hash);

/**
 * @brief Compute SHA-256 hash of data in one call
 * @param data Data to hash
 * @param data_len Length of data
 * @param hash Output buffer for hash (must be at least NETCHUNK_SHA256_DIGEST_LENGTH bytes)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_sha256_hash(const uint8_t* data,
    size_t data_len,
    uint8_t* hash);

/**
 * @brief Compute SHA-256 hash of file
 * @param file_path Path to file
 * @param hash Output buffer for hash (must be at least NETCHUNK_SHA256_DIGEST_LENGTH bytes)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_sha256_hash_file(const char* file_path,
    uint8_t* hash);

// Utility Functions

/**
 * @brief Convert hash to hexadecimal string
 * @param hash Hash bytes
 * @param hash_len Length of hash
 * @param hex_string Output buffer for hex string (must be at least hash_len*2 + 1 bytes)
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_hash_to_hex_string(const uint8_t* hash,
    size_t hash_len,
    char* hex_string);

/**
 * @brief Convert hexadecimal string to hash bytes
 * @param hex_string Hex string input
 * @param hash Output buffer for hash bytes
 * @param hash_len Length of hash buffer
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_hex_string_to_hash(const char* hex_string,
    uint8_t* hash,
    size_t hash_len);

/**
 * @brief Compare two hashes
 * @param hash1 First hash
 * @param hash2 Second hash
 * @param hash_len Length of hashes
 * @return true if hashes match, false otherwise
 */
bool netchunk_hash_compare(const uint8_t* hash1,
    const uint8_t* hash2,
    size_t hash_len);

/**
 * @brief Generate secure random bytes
 * @param buffer Output buffer for random bytes
 * @param buffer_size Size of buffer
 * @return NETCHUNK_SUCCESS on success, error code on failure
 */
netchunk_error_t netchunk_generate_random_bytes(uint8_t* buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // NETCHUNK_CRYPTO_H
