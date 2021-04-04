#include <stdint.h>

typedef struct {
    uint8_t        hash[32];
    uint32_t    buffer[16];
    uint32_t    state[8];
    uint8_t        length[8];
} sha256;

void sha256_initialize(sha256 *sha) {
    for (int i = 0; i < 16; i++) {
        sha->buffer[i] = 0;
    }
    sha->state[0] = 0x6a09e667;
    sha->state[1] = 0xbb67ae85;
    sha->state[2] = 0x3c6ef372;
    sha->state[3] = 0xa54ff53a;
    sha->state[4] = 0x510e527f;
    sha->state[5] = 0x9b05688c;
    sha->state[6] = 0x1f83d9ab;
    sha->state[7] = 0x5be0cd19;
    for (int i = 0; i < 8; i++) {
        sha->length[i] = 0;
    }
}

void sha256_update(sha256 *sha,
                   const uint8_t *message,
                   uint32_t length) {
    for (int i = 7; i >= 0; i--) {
        int bits;
        if (i == 7)
            bits = length << 3;
        else if (i == 0 || i == 1 || i == 2)
            bits = 0;
        else
            bits = length >> (53 - 8 * i);
        bits &= 0xff;
        if (sha->length[i] + bits > 0xff) {
            for (int j = i - 1; j >= 0 && sha->length[j]++ == 0xff; --j);
        }
        sha->length[i] += bits;
    }
    while (length > 0) {
        int index = sha->length[6] % 2 * 32 + sha->length[7] / 8;
        index = (index + 64 - length % 64) % 64;
        for (;length > 0 && index < 64; ++message, ++index, --length) {
            sha->buffer[index / 4] |= *message << (24 - index % 4 * 8);
        }
        if (index == 64) {
            const uint32_t k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
                0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
                0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
                0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
                0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
                0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
                0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
                0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
            };
            uint32_t w[64], a, b, c, d, e, f, g, h;
            int t;
            for (t = 0; t < 16; ++t) {
                w[t] = sha->buffer[t];
                sha->buffer[t] = 0;
            }
            for (t = 16; t < 64; ++t) {
                uint32_t s0, s1;
                s0 = (w[t - 15] >> 7 | w[t - 15] << 25);
                s0 ^= (w[t - 15] >> 18 | w[t - 15] << 14);
                s0 ^= (w[t - 15] >> 3);
                s1 = (w[t - 2] >> 17 | w[t - 2] << 15);
                s1 ^= (w[t - 2] >> 19 | w[t - 2] << 13);
                s1 ^= (w[t - 2] >> 10);
                w[t] = (s1 + w[t - 7] + s0 + w[t - 16]) & 0xffffffffU;
            }
            a = sha->state[0];
            b = sha->state[1];
            c = sha->state[2];
            d = sha->state[3];
            e = sha->state[4];
            f = sha->state[5];
            g = sha->state[6];
            h = sha->state[7];
            for (t = 0; t < 64; ++t) {
                uint32_t e0, e1, t1, t2;
                e0 = (a >> 2 | a << 30);
                e0 ^= (a >> 13 | a << 19);
                e0 ^= (a >> 22 | a << 10);
                e1 = (e >> 6 | e << 26);
                e1 ^= (e >> 11 | e << 21);
                e1 ^= (e >> 25 | e << 7);
                t1 = h + e1 + ((e & f) ^ (~e & g)) + k[t] + w[t];
                t2 = e0 + ((a & b) ^ (a & c) ^ (b & c));
                h = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }
            sha->state[0] = (sha->state[0] + a) & 0xffffffffU;
            sha->state[1] = (sha->state[1] + b) & 0xffffffffU;
            sha->state[2] = (sha->state[2] + c) & 0xffffffffU;
            sha->state[3] = (sha->state[3] + d) & 0xffffffffU;
            sha->state[4] = (sha->state[4] + e) & 0xffffffffU;
            sha->state[5] = (sha->state[5] + f) & 0xffffffffU;
            sha->state[6] = (sha->state[6] + g) & 0xffffffffU;
            sha->state[7] = (sha->state[7] + h) & 0xffffffffU;
        }
    }
}

void sha256_finalize(sha256 *sha,
                     const uint8_t *message,
                     uint32_t length) {
    uint8_t terminator[64 + 8] = { 0x80 };
    if (length > 0) {
        sha256_update(sha, message, length);
    }
    length = 64 - sha->length[6] % 2 * 32 - sha->length[7] / 8;
    if (length < 9) length += 64;
    for (int i = 0; i < 8; ++i) {
        terminator[length - 8 + i] = sha->length[i];
    }
    sha256_update(sha, terminator, length);
    for (int i = 0; i < 32; ++i) {
        sha->hash[i] = (sha->state[i / 4] >> (24 - 8 * (i % 4))) & 0xff;
    }
}

void sha256_get(uint8_t hash[32],
                const uint8_t *message,
                int length) {
    sha256 sha;
    sha256_initialize(&sha);
    sha256_finalize(&sha, message, length);
    for (int i = 0; i < 32; ++i) hash[i] = sha.hash[i];
}

typedef struct _hmac_sha256 {
    uint8_t    digest[32];
    uint8_t    key[64];
    sha256    sha;
} hmac_sha256;

void hmac_sha256_initialize(hmac_sha256 *hmac,
                            const uint8_t *key, int length) {
    int i;
    if (length <= 64) {
        for (i = 0; i < length; ++i) hmac->key[i] = key[i] ^ 0x36;
        for (; i < 64; ++i) hmac->key[i] = 0x36;
    } else {
        sha256_initialize(&(hmac->sha));
        sha256_finalize(&(hmac->sha), key, length);
        for (i = 0; i < 32; ++i) hmac->key[i] = hmac->sha.hash[i] ^ 0x36;
        for (; i < 64; ++i) hmac->key[i] = 0x36;
    }
    sha256_initialize(&(hmac->sha));
    sha256_update(&(hmac->sha), hmac->key, 64);
}

void hmac_sha256_update(hmac_sha256 *hmac,
                        const uint8_t *message, int length) {
    sha256_update(&(hmac->sha), message, length);
}

void hmac_sha256_finalize(hmac_sha256 *hmac,
                          const uint8_t *message, int length) {
    sha256_finalize(&(hmac->sha), message, length);
    for (int i = 0; i < 32; ++i) {
        hmac->digest[i] = hmac->sha.hash[i];
    }
    for (int i = 0; i < 64; ++i) {
        hmac->key[i] ^= (0x36 ^ 0x5c);
    }
    sha256_initialize(&(hmac->sha));
    sha256_update(&(hmac->sha), hmac->key, 64);
    sha256_finalize(&(hmac->sha), hmac->digest, 32);
    for (int i = 0; i < 32; ++i) {
        hmac->digest[i] = hmac->sha.hash[i];
    }
}

void hmac_sha256_get(uint8_t digest[32],
                     uint8_t *message, int message_length,
                     uint8_t *key, int key_length) {
    hmac_sha256 hmac;
    hmac_sha256_initialize(&hmac, key, key_length);
    hmac_sha256_finalize(&hmac, message, message_length);
    for (int i = 0; i < 32; ++i) {
        digest[i] = hmac.digest[i];
    }
}
