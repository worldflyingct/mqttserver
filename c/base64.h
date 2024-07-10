#ifndef __BASE64_H__
#define __BASE64_H__

int base64_encode (const uint8_t* data, uint32_t size, uint8_t* base64, uint32_t *length);
int base64_decode (const uint8_t* base64, uint32_t length, uint8_t *data, uint32_t *size);

#endif
