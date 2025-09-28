#include "crypto.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// SHA-256 constants
static const uint32_t SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// Internal helper functions
static void sha256_transform(netchunk_sha256_context_t* context, const uint8_t data[64]);
static uint32_t sha256_rotr(uint32_t value, uint32_t amount);
static uint32_t sha256_choose(uint32_t x, uint32_t y, uint32_t z);
static uint32_t sha256_majority(uint32_t x, uint32_t y, uint32_t z);
static uint32_t sha256_sig0(uint32_t x);
static uint32_t sha256_sig1(uint32_t x);
static void uint32_to_bytes(uint32_t val, uint8_t* bytes);
static uint32_t bytes_to_uint32(const uint8_t* bytes);

// Hash Functions

netchunk_error_t netchunk_sha256_init(netchunk_sha256_context_t* context)
{
    if (!context) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    context->count = 0;
    memcpy(context->state, SHA256_H0, sizeof(SHA256_H0));
    memset(context->buffer, 0, sizeof(context->buffer));

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_sha256_update(netchunk_sha256_context_t* context,
    const uint8_t* data,
    size_t data_len)
{
    if (!context || !data) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    if (data_len == 0) {
        return NETCHUNK_SUCCESS;
    }

    size_t buffer_space = NETCHUNK_SHA256_BLOCK_SIZE - (context->count % NETCHUNK_SHA256_BLOCK_SIZE);
    size_t input_len = data_len;
    const uint8_t* input = data;

    context->count += data_len;

    // If we have data in buffer and new data fills it
    if (buffer_space <= input_len) {
        memcpy(context->buffer + (NETCHUNK_SHA256_BLOCK_SIZE - buffer_space), input, buffer_space);
        sha256_transform(context, context->buffer);
        input += buffer_space;
        input_len -= buffer_space;

        // Process complete 64-byte blocks
        while (input_len >= NETCHUNK_SHA256_BLOCK_SIZE) {
            sha256_transform(context, input);
            input += NETCHUNK_SHA256_BLOCK_SIZE;
            input_len -= NETCHUNK_SHA256_BLOCK_SIZE;
        }

        // Store remaining data in buffer
        memcpy(context->buffer, input, input_len);
    } else {
        // Just store data in buffer
        memcpy(context->buffer + (NETCHUNK_SHA256_BLOCK_SIZE - buffer_space), input, input_len);
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_sha256_final(netchunk_sha256_context_t* context,
    uint8_t* hash)
{
    if (!context || !hash) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    uint64_t bit_len = context->count * 8;
    size_t buffer_pos = context->count % NETCHUNK_SHA256_BLOCK_SIZE;

    // Append the '1' bit (0x80)
    context->buffer[buffer_pos++] = 0x80;

    // If we don't have room for the length, pad and transform
    if (buffer_pos > 56) {
        memset(context->buffer + buffer_pos, 0, NETCHUNK_SHA256_BLOCK_SIZE - buffer_pos);
        sha256_transform(context, context->buffer);
        memset(context->buffer, 0, 56);
    } else {
        memset(context->buffer + buffer_pos, 0, 56 - buffer_pos);
    }

    // Append length in big-endian format
    uint32_to_bytes((uint32_t)(bit_len >> 32), context->buffer + 56);
    uint32_to_bytes((uint32_t)(bit_len & 0xffffffff), context->buffer + 60);

    sha256_transform(context, context->buffer);

    // Produce the final hash value in big-endian format
    for (int i = 0; i < 8; i++) {
        uint32_to_bytes(context->state[i], hash + (i * 4));
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_sha256_hash(const uint8_t* data,
    size_t data_len,
    uint8_t* hash)
{
    if (!data || !hash) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    netchunk_sha256_context_t context;
    netchunk_error_t error;

    error = netchunk_sha256_init(&context);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    error = netchunk_sha256_update(&context, data, data_len);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    error = netchunk_sha256_final(&context, hash);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_sha256_hash_file(const char* file_path,
    uint8_t* hash)
{
    if (!file_path || !hash) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    FILE* file = fopen(file_path, "rb");
    if (!file) {
        return NETCHUNK_ERROR_FILE_NOT_FOUND;
    }

    netchunk_sha256_context_t context;
    netchunk_error_t error = netchunk_sha256_init(&context);
    if (error != NETCHUNK_SUCCESS) {
        fclose(file);
        return error;
    }

    uint8_t buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        error = netchunk_sha256_update(&context, buffer, bytes_read);
        if (error != NETCHUNK_SUCCESS) {
            fclose(file);
            return error;
        }
    }

    if (ferror(file)) {
        fclose(file);
        return NETCHUNK_ERROR_FILE_ACCESS;
    }

    fclose(file);

    error = netchunk_sha256_final(&context, hash);
    if (error != NETCHUNK_SUCCESS) {
        return error;
    }

    return NETCHUNK_SUCCESS;
}

// Utility Functions

netchunk_error_t netchunk_hash_to_hex_string(const uint8_t* hash,
    size_t hash_len,
    char* hex_string)
{
    if (!hash || !hex_string || hash_len == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < hash_len; i++) {
        sprintf(hex_string + (i * 2), "%02x", hash[i]);
    }
    hex_string[hash_len * 2] = '\0';

    return NETCHUNK_SUCCESS;
}

netchunk_error_t netchunk_hex_string_to_hash(const char* hex_string,
    uint8_t* hash,
    size_t hash_len)
{
    if (!hex_string || !hash || hash_len == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    size_t hex_len = strlen(hex_string);
    if (hex_len != hash_len * 2) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < hash_len; i++) {
        char byte_str[3] = { hex_string[i * 2], hex_string[i * 2 + 1], '\0' };
        char* endptr;
        unsigned long byte_val = strtoul(byte_str, &endptr, 16);

        if (*endptr != '\0' || byte_val > 255) {
            return NETCHUNK_ERROR_INVALID_ARGUMENT;
        }

        hash[i] = (uint8_t)byte_val;
    }

    return NETCHUNK_SUCCESS;
}

bool netchunk_hash_compare(const uint8_t* hash1,
    const uint8_t* hash2,
    size_t hash_len)
{
    if (!hash1 || !hash2 || hash_len == 0) {
        return false;
    }

    return memcmp(hash1, hash2, hash_len) == 0;
}

netchunk_error_t netchunk_generate_random_bytes(uint8_t* buffer,
    size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return NETCHUNK_ERROR_INVALID_ARGUMENT;
    }

    // Try to use /dev/urandom first
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd != -1) {
        ssize_t bytes_read = read(fd, buffer, buffer_size);
        close(fd);

        if (bytes_read == (ssize_t)buffer_size) {
            return NETCHUNK_SUCCESS;
        }
    }

    // Fallback to a simple pseudo-random approach
    // Note: This is not cryptographically secure and should be replaced
    // with a proper PRNG in production
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    for (size_t i = 0; i < buffer_size; i++) {
        buffer[i] = (uint8_t)(rand() & 0xFF);
    }

    return NETCHUNK_SUCCESS;
}

// Internal helper function implementations

static void sha256_transform(netchunk_sha256_context_t* context, const uint8_t data[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;

    // Copy chunk into first 16 words W[0..15] of the message schedule array
    for (int i = 0; i < 16; i++) {
        w[i] = bytes_to_uint32(data + (i * 4));
    }

    // Extend the first 16 words into the remaining 48 words W[16..63] of the message schedule array
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    // Initialize hash value for this chunk
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    // Main loop
    for (int i = 0; i < 64; i++) {
        t1 = h + sha256_sig1(e) + sha256_choose(e, f, g) + SHA256_K[i] + w[i];
        t2 = sha256_sig0(a) + sha256_majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Add the compressed chunk to the current hash value
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static uint32_t sha256_rotr(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32 - amount));
}

static uint32_t sha256_choose(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

static uint32_t sha256_majority(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t sha256_sig0(uint32_t x)
{
    return sha256_rotr(x, 2) ^ sha256_rotr(x, 13) ^ sha256_rotr(x, 22);
}

static uint32_t sha256_sig1(uint32_t x)
{
    return sha256_rotr(x, 6) ^ sha256_rotr(x, 11) ^ sha256_rotr(x, 25);
}

static void uint32_to_bytes(uint32_t val, uint8_t* bytes)
{
    bytes[0] = (uint8_t)(val >> 24);
    bytes[1] = (uint8_t)(val >> 16);
    bytes[2] = (uint8_t)(val >> 8);
    bytes[3] = (uint8_t)val;
}

static uint32_t bytes_to_uint32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}
