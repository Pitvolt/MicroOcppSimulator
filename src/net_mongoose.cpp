// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "net_mongoose.h"
#include "evse.h"
#include "api.h"
#include <MicroOcppMongooseClient.h>
#include <string>
#include <ArduinoJson.h>
#include <MicroOcpp.h>
#include <MicroOcpp/Debug.h>
#include <MicroOcpp/Model/Configuration/ConfigurationService.h>

//cors_headers allow the browser to make requests from any domain, allowing all headers and all methods
#define DEFAULT_HEADER "Content-Type: application/json\r\n"
#define CORS_HEADERS "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers:Access-Control-Allow-Headers, Origin,Accept, X-Requested-With, Content-Type, Access-Control-Request-Method, Access-Control-Request-Headers\r\nAccess-Control-Allow-Methods: GET,HEAD,OPTIONS,POST,PUT\r\n"

MO_MG_Connection *wsClient = nullptr;
const char *api_cert = "";
const char *api_key = "";
const char *api_user = "";
const char *api_pass = "";

void server_initialize(MO_MG_Connection *wsClientHandle, const char *cert, const char *key, const char *user, const char *pass) {
    wsClient = wsClientHandle;
    api_cert = cert;
    api_key = key;
    api_user = user;
    api_pass = pass;
}

bool api_check_basic_auth(const char *user, const char *pass) {
    if (strcmp(api_user, user)) {
        return false;
    }
    if (strcmp(api_pass, pass)) {
        return false;
    }
    return true;
}

void http_serve(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_ACCEPT) {
        if (mg_url_is_ssl((const char*)c->fn_data)) {  // TLS listener!
            MO_DBG_VERBOSE("API TLS setup");
            struct mg_tls_opts opts = {0};
            opts.cert = mg_str(api_cert);
            opts.key = mg_str(api_key);
            mg_tls_init(c, &opts);
        }
    } else if (ev == MG_EV_HTTP_MSG) {
        //struct mg_http_message *message_data = (struct mg_http_message *) ev_data;
        struct mg_http_message *message_data = reinterpret_cast<struct mg_http_message *>(ev_data);
        const char *final_headers = DEFAULT_HEADER CORS_HEADERS;

        char user[64], pass[64];
        mg_http_creds(message_data, user, sizeof(user), pass, sizeof(pass));
        if (!api_check_basic_auth(user, pass)) {
            mg_http_reply(c, 401, final_headers, "Unauthorized. Expect Basic Auth user and / or password\n");
            return;
        }

        struct mg_str json = message_data->body;

        MO_DBG_VERBOSE("%.*s", 20, message_data->uri.buf);

        MicroOcpp::Method method = MicroOcpp::Method::UNDEFINED;

        if (!mg_strcasecmp(message_data->method, mg_str("POST"))) {
            method = MicroOcpp::Method::POST;
            MO_DBG_VERBOSE("POST");
        } else if (!mg_strcasecmp(message_data->method, mg_str("GET"))) {
            method = MicroOcpp::Method::GET;
            MO_DBG_VERBOSE("GET");
        }

        //start different api endpoints
        if(mg_match(message_data->uri, mg_str("/api/websocket"), NULL)){
            MO_DBG_VERBOSE("query websocket");

            if (method == MicroOcpp::Method::POST) {
                if (auto val = mg_json_get_str(json, "$.backendUrl")) {
                    mo_setBackendUrl(wsClient, val);
                }
                if (auto val = mg_json_get_str(json, "$.chargeBoxId")) {
                    mo_setChargeBoxId(wsClient, val);
                }
                if (auto val = mg_json_get_str(json, "$.authorizationKey")) {
                    mo_setAuthKey(wsClient, val);
                }
                mo_reloadUrl(wsClient);
                {
                    auto val = mg_json_get_long(json, "$.pingInterval", -1);
                    if (val > 0) {
                        mo_setVarConfigInt(mo_getApiContext(), "OCPPCommCtrlr", "WebSocketPingInterval", "WebSocketPingInterval", val);
                    }
                }
                {
                    auto val = mg_json_get_long(json, "$.reconnectInterval", -1);
                    if (val > 0) {
                        mo_setVarConfigInt(mo_getApiContext(), "OCPPCommCtrlr", "RetryBackOffWaitMinimum", MO_CONFIG_EXT_PREFIX "ReconnectInterval", val);
                    }
                }
                if (auto val = mg_json_get_str(json, "$.dnsUrl")) {
                    MO_DBG_WARN("dnsUrl not implemented");
                    (void)val;
                }
            }
            StaticJsonDocument<256> doc;
            doc["backendUrl"] = mo_getBackendUrl(wsClient);
            doc["chargeBoxId"] = mo_getChargeBoxId(wsClient);
            doc["authorizationKey"] = mo_getAuthKey(wsClient);
            int pingInterval = 0;
            mo_getVarConfigInt(mo_getApiContext(), "OCPPCommCtrlr", "WebSocketPingInterval", "WebSocketPingInterval", &pingInterval);
            doc["pingInterval"] = pingInterval;
            int reconnectInterval = 0;
            mo_getVarConfigInt(mo_getApiContext(), "OCPPCommCtrlr", "RetryBackOffWaitMinimum", MO_CONFIG_EXT_PREFIX "ReconnectInterval", &reconnectInterval);
            doc["reconnectInterval"] = reconnectInterval;
            std::string serialized;
            serializeJson(doc, serialized);
            mg_http_reply(c, 200, final_headers, serialized.c_str());
            return;
        } else if (strncmp(message_data->uri.buf, "/api", strlen("api")) == 0) {
            #define RESP_BUF_SIZE 8192
            char resp_buf [RESP_BUF_SIZE];

            //replace endpoint-body separator by null
            if (char *c = strchr((char*) message_data->uri.buf, ' ')) {
                *c = '\0';
            }

            int status = 404;
            if (status == 404) {
                status = mocpp_api2_call(
                    message_data->uri.buf + strlen("/api"),
                    message_data->uri.len - strlen("/api"),
                    method,
                    message_data->query.buf,
                    message_data->query.len,
                    resp_buf, RESP_BUF_SIZE);
            }
            if (status == 404) {
                status = mocpp_api_call(
                    message_data->uri.buf + strlen("/api"),
                    method,
                    message_data->body.buf,
                    resp_buf, RESP_BUF_SIZE);
            }

            mg_http_reply(c, status, final_headers, resp_buf);
        } else if (mg_match(message_data->uri, mg_str("/"), NULL)) { //if no specific path is given serve dashboard application file
            struct mg_http_serve_opts opts;
            memset(&opts, 0, sizeof(opts));
            opts.root_dir = "./public";
            opts.extra_headers = "Content-Type: text/html\r\nContent-Encoding: gzip\r\n";
            mg_http_serve_file(c, message_data, "public/bundle.html.gz", &opts);
        } else {
            mg_http_reply(c, 404, final_headers, "API endpoint not found");
        }
    }
}

struct mg_mgr *rmt_ctrl_mgr;
std::string rmt_ctrl_url;
std::string rmt_ctrl_auth_token;
std::string rmt_ctrl_ca;

struct mg_connection *rmt_ctrl_conn;
int32_t rmt_ctrl_reconnect_timer;

void rmt_ctrl_process_msg(const char *msg, size_t msg_len) {
    DynamicJsonDocument doc (1024);

    auto err = deserializeJson(doc, msg, msg_len);
    if (err) {
        MO_DBG_WARN("JSON deserialization error: %s", err.c_str());
        return;
    }

    const char *time = doc["time"] | "_Invalid";
    const char *id = doc["id"] | "_Invalid";
    const char *operation = doc["operation"] | "_Invalid";

    MO_DBG_INFO("Rmt Ctrl %s (%s)", operation, time);

    std::vector<const char*> params_key, params_val;

    JsonArray params_json = doc["params"];
    for (size_t i = 0; i < params_json.size(); i++) {
        JsonObject key_value = params_json[i];
        const char *key = key_value["key"] | (const char*)nullptr;
        const char *value = key_value["value"] | (const char*)nullptr;
        if (!key || !value) {
            MO_DBG_ERR("url encoder err");
            continue;
        }

        params_key.push_back(key);
        params_val.push_back(value);
    }
    
    const char *type = doc["type"] | "_Invalid";

    bool success = mocpp_api3_call(type, operation, &params_key[0], &params_val[0], params_key.size());

    if (!success) {
        MO_DBG_ERR("API call failed");
    }

    //Send response (this function is only called when connection is esablished, so can send response directly)

    DynamicJsonDocument doc_response (256);
    doc_response["id"] = id;
    doc_response["status"] = success ? "Ok" : "Nok";

    char response [256];
    auto serialize_ret = serializeJson(doc_response, response, sizeof(response));
    if (serialize_ret < 2 || serialize_ret >= sizeof(response)) {
        MO_DBG_ERR("JSON serialization error");
        return;
    }

    mg_ws_send(rmt_ctrl_conn, response, serialize_ret, WEBSOCKET_OP_TEXT);
}

void rmt_ctrl_mongoose_cb(struct mg_connection *c, int ev, void *ev_data) {

    if (ev == MG_EV_CONNECT) {
        // If target URL is SSL/TLS, command client connection to use TLS
        if (mg_url_is_ssl(rmt_ctrl_url.c_str())) {
            const char *ca_string = rmt_ctrl_ca.c_str();
            if (ca_string && *ca_string == '\0') { //check if certificate verification is disabled (cert string is empty)
                //yes, disabled
                ca_string = nullptr;
            }
            struct mg_tls_opts opts;
            memset(&opts, 0, sizeof(struct mg_tls_opts));
            opts.ca = mg_str(ca_string);
            opts.name = mg_url_host(rmt_ctrl_url.c_str());
            mg_tls_init(c, &opts);
        } else {
            MO_DBG_WARN("Insecure connection (WS)");
        }
    } else if (ev == MG_EV_WS_OPEN) {
        MO_DBG_INFO("Rmt Ctrl connected!");
        MO_DBG_DEBUG("(connection %s)", rmt_ctrl_url.c_str());
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        rmt_ctrl_process_msg((const char*) wm->data.buf, wm->data.len);
    } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
        MO_DBG_INFO("Rmt Ctrl connection %s", ev == MG_EV_CLOSE ? "closed" : "error");
        rmt_ctrl_conn = nullptr;
        if (mo_isInitialized()) {
            rmt_ctrl_reconnect_timer = mo_getUptime() + 15;
        }
    }
}

bool rmt_ctrl_initialize(struct mg_mgr *mgr, const char *url, const char *auth_token, const char *ca) {
    rmt_ctrl_mgr = mgr;
    rmt_ctrl_url = url ? url : "";
    rmt_ctrl_auth_token = auth_token ? auth_token : "";
    rmt_ctrl_ca = ca ? ca : "";
    return true;
}

void rmt_ctrl_loop() {
    if (!mo_isInitialized()) {
        return;
    }

    if (!*rmt_ctrl_url.c_str()) {
        return;
    }

    if (!rmt_ctrl_conn && mo_getUptime() >= rmt_ctrl_reconnect_timer) {
        MO_DBG_INFO("Rmt Ctrl create connection");
        MO_DBG_DEBUG("(connection %s)", rmt_ctrl_url.c_str());
        rmt_ctrl_reconnect_timer += 15;

        rmt_ctrl_conn = mg_ws_connect(
            rmt_ctrl_mgr, 
            rmt_ctrl_url.c_str(), 
            rmt_ctrl_mongoose_cb, 
            nullptr, 
            "Authorization: Bearer %s\r\n", rmt_ctrl_auth_token.c_str());

        MO_DBG_ERR("Rmt Ctrl connection failure");
    }
}

void rmt_ctrl_deinitialize() {
    if (rmt_ctrl_conn) {
        rmt_ctrl_conn->is_closing = 1;
    }
    rmt_ctrl_conn = nullptr;
    rmt_ctrl_mgr = nullptr;
    rmt_ctrl_url.clear();
    rmt_ctrl_auth_token.clear();
}
