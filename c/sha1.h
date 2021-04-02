#ifndef _SHA1_H_
#define _SHA1_H_
#include <stdint.h>

int sha1( const unsigned char *input, size_t ilen, unsigned char output[20] );

#endif
