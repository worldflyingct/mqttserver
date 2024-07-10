#ifndef __WS_H__
#define __WS_H__

struct HTTPPARAM {
    uint8_t *key;
    uint8_t *value;
};

int Ws_Create ();

#endif
