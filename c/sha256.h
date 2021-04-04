#ifndef __sha256_h__
#define __sha256_h__

void sha256_get(uint8_t hash[32],
            const uint8_t *message,
            int length);
void hmac_sha256_get(uint8_t digest[32],
                uint8_t *message, int message_length,
                uint8_t *key, int key_length);
#endif
