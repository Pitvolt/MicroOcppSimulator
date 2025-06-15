// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#ifndef MO_NET_MONGOOSE_H
#define MO_NET_MONGOOSE_H

#if MO_NETLIB == MO_NETLIB_MONGOOSE

#include "mongoose.h"
#include <MicroOcppMongooseClient.h>

void server_initialize(MO_MG_Connection *osock, const char *cert = "", const char *key = "", const char *user = "", const char *pass = "");

void http_serve(struct mg_connection *c, int ev, void *ev_data);

#endif //MO_NETLIB == MO_NETLIB_MONGOOSE

#endif
