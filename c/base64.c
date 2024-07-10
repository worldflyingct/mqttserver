#include <stdint.h>

const uint8_t* base64table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint8_t gettableindex (uint8_t param) {
    if ('A' <= param && param <= 'Z') {
        return param - 'A';
    } else if ('a' <= param && param <= 'z') {
        return param - 'a' + 26;
    } else if ('0' <= param && param <= '9') {
        return param - '0' + 52;
    } else if (param == '+') {
        return 62;
    } else if (param == '/') {
        return 63;
    } else if (param == '=') {
        return 0;
    } else {
        return 0xff;
    }
}

int base64_encode (const uint8_t* data, uint32_t size, uint8_t* base64, uint32_t *length) {
    uint32_t i, j = 0;
    uint32_t len = size;
    while (len % 3) {
        ++len;
    }
    len = 4 * len / 3;
    if (len > *length) {
        return -1;
    }
    for (i = 2 ; i < size ; i+=3) {
        base64[j] = base64table[(data[i-2] & 0xfc) >> 2];
        ++j;
        base64[j] = base64table[((data[i-1] & 0xf0) >> 4) | ((data[i-2] & 0x03) << 4)];
        ++j;
        base64[j] = base64table[((data[i] & 0xc0) >> 6) | ((data[i-1] & 0x0f) << 2)];
        ++j;
        base64[j] = base64table[data[i] & 0x3f];
        ++j;
    }
    if (size % 3) {
        base64[j] = base64table[(data[i-2] & 0xfc) >> 2];
        ++j;
        if (size % 3 == 2) {
            base64[j] = base64table[((data[i-1] & 0xf0) >> 4) | ((data[i-2] & 0x03) << 4)];
            ++j;
            base64[j] = base64table[(data[i-1] & 0x0f) << 2];
            ++j;
        } else {
            base64[j] = base64table[(data[i-2] & 0x03) << 4];
            ++j;
            base64[j] = '=';
            ++j;
        }
        base64[j] = '=';
        ++j;
    }
    *length = j;
    return 0;
}

int base64_decode (const uint8_t* base64, uint32_t length, uint8_t *data, uint32_t *size) {
    uint32_t i, j, len;
    if (base64[length-2] == '=') {
        len = 3 * length / 4 - 2;
    } else if (base64[length-1] == '=') {
        len = 3 * length / 4 - 1;
    } else {
        len = 3 * length / 4;
    }
    if (len > *size) {
        return -1;
    }
    for (i = 3, j = 0 ; i < length ; i+=4) {
        uint8_t tmp1 = gettableindex(base64[i-3]);
        uint8_t tmp2 = gettableindex(base64[i-2]);
        uint8_t tmp3 = gettableindex(base64[i-1]);
        uint8_t tmp4 = gettableindex(base64[i]);
        data[j] = ((tmp1 & 0x3f) << 2) | ((tmp2 & 0x30) >> 4);
        ++j;
        if (j == len) {
            break;
        }
        data[j] = ((tmp2 & 0x0f) << 4) | ((tmp3 & 0x3c) >> 2);
        ++j;
        if (j == len) {
            break;
        }
        data[j] = ((tmp3 & 0x03) << 6) | (tmp4 & 0x3f);
        ++j;
    }
    *size = j;
    return 0;
}
