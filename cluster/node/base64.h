#ifndef __BASE64_H__
#define __BASE64_H__

int base64_encode (const unsigned char* data, unsigned int size, unsigned char* base64, unsigned int *length);
int base64_decode (const unsigned char* base64, unsigned int length, unsigned char *data, unsigned int *size);

#endif
