//
// Created by Fleming on 2024-08-09.
//

#ifndef CTWISTED_PROTOCOL_H
#define CTWISTED_PROTOCOL_H

#include "base.h"


typedef struct Protocol Protocol;
typedef struct ProtocolFactory ProtocolFactory;
typedef struct Transport Transport;

struct Protocol {
    void (*data_received)(Protocol *self, const char *data, size_t len);
    void (*connection_made)(Protocol *self);
    void (*connection_lost)(Protocol *self);
    void (*data_send)(Protocol *self, const char *data, size_t len); // Send function delegates to Transport

    ProtocolFactory *factory;
    Transport *transport;
};

struct ProtocolFactory {
    Protocol *(*build_protocol)(ProtocolFactory *self);
    void (*destroy)(ProtocolFactory *self);
};

#endif //CTWISTED_PROTOCOL_H
