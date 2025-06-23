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

//Remote control interface: Simulator establishes a WebSocket connection with a Test driver.
//Test driver sends messages to Simulator to trigger simulated actions (e.g. EV plug in)
bool rmt_ctrl_initialize(struct mg_mgr *mgr, const char *url, const char *auth_token, const char *ca);

void rmt_ctrl_loop();

void rmt_ctrl_deinitialize();

#endif //MO_NETLIB == MO_NETLIB_MONGOOSE

#endif
