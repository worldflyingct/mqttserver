#include <stdio.h>
#include "config.h"
#include "event_poll.h"
#include "ws.h"
#include "tcp.h"
#include "smalloc.h"

int main () {
    if (setmemcheck()) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    struct ConfigData *configdata = InitConfig();
    if (!configdata) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    if (event_poll_create()) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        return -2;
    }
    if (configdata->tlsport != 0 || configdata->wssport != 0) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
        if (!SSL_CTX_use_certificate_file(ctx, configdata->crtpath, SSL_FILETYPE_PEM)) {
            printf("SSL use certificate file error, in %s, at %d\n", __FILE__, __LINE__);
            return -3;
        }
        if (!SSL_CTX_use_PrivateKey_file(ctx, configdata->keypath, SSL_FILETYPE_PEM) ) {
            printf("SSL use PrivateKey file error, in %s, at %d\n", __FILE__, __LINE__);
            return -4;
        }
        if (!SSL_CTX_check_private_key(ctx)) {
            printf("SSL check private key error, in %s, at %d\n", __FILE__, __LINE__);
            return -5;
        }
        configdata->ctx = ctx;
    }
    if (Tcp_Create()) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        return -6;
    }
    if (Ws_Create()) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        return -7;
    }
    event_poll_loop();
    return 0;
}
