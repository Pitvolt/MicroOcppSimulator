// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "api.h"
#include "mongoose.h"

#include <MicroOcpp.h>
#include <MicroOcpp/Debug.h>
#include <MicroOcpp/Core/Memory.h>
#include <MicroOcpp/Model/Common/EvseId.h>
#include <MicroOcpp/Model/Authorization/IdToken.h>

#include "evse.h"

void (*mocpp_api_reboot_cb)();

int32_t m_evse_ready_time [MO_NUM_EVSEID];
int32_t m_ev_ready_time [MO_NUM_EVSEID];

std::vector<std::pair<int32_t,std::function<void(void)>>> mocpp_api_timer;

void mocpp_api_schedule_event(int32_t delaySecs, std::function<void(void)> event) {
    mocpp_api_timer.emplace_back(mo_getUptime() + delaySecs, event);
}

//simple matching function; takes * as a wildcard
bool str_match(const char *query, const char *pattern) {
    size_t qi = 0, pi = 0;
    
    while (pattern[pi]) {
        if (query[qi] && query[qi] == pattern[pi]) {
            qi++;
            pi++;
        } else if (pattern[pi] == '*') {
            while (pattern[pi] == '*') pi++;
            while (query[qi] != pattern[pi]) qi++;
        } else {
            break;
        }
    }

    return !query[qi] && !pattern[pi];
}

void mocpp_api_set_reboot_cb(void (*reboot_cb)()) {
    mocpp_api_reboot_cb = reboot_cb;
}

void mocpp_api_loop() {
    for (auto it = mocpp_api_timer.begin(); it != mocpp_api_timer.end();) {
        if (it->first > mo_getUptime()) {
            it->second();
            it = mocpp_api_timer.erase(it);
        } else {
            it++;
        }
    }

    for (unsigned int i = 1; i < MO_NUM_EVSEID; i++) {
        if (connectors[i].getEvPlugged() && mo_ocppPermitsCharge2(mo_getApiContext(), i)) {
            
            // schedule EVSE readiness
            if (!m_evse_ready_time[i]) {
                m_evse_ready_time[i] = mo_getUptime() + 1;
            }

            // apply EVSE readiness
            if (m_evse_ready_time[i] >= mo_getUptime() && !connectors[i].getEvseReady()) {
                MO_DBG_DEBUG("TRACE");
                connectors[i].setEvseReady(true);
            }
        } else {
            if (connectors[i].getEvseReady()) {
                MO_DBG_DEBUG("TRACE");
                connectors[i].setEvseReady(false);
            }
            m_evse_ready_time[i] = 0;
        }
        
        if (connectors[i].getEvPlugged() && mo_ocppPermitsCharge2(mo_getApiContext(), i) && connectors[i].getEvseReady()) {
            
            // schedule EV readiness
            if (!m_ev_ready_time[i]) {
                m_ev_ready_time[i] = mo_getUptime() + 1;
            }

            // apply EV readiness
            if (m_ev_ready_time[i] >= mo_getUptime() && !connectors[i].getEvReady()) {
                MO_DBG_DEBUG("TRACE");
                connectors[i].setEvReady(true);
            }
        } else {
            if (connectors[i].getEvReady()) {
                MO_DBG_DEBUG("TRACE");
                connectors[i].setEvReady(false);
            }
            m_ev_ready_time[i] = 0;
        }
    }
}

int mocpp_api_call(const char *endpoint, MicroOcpp::Method method, const char *body, char *resp_body, size_t resp_body_size) {
    
    MO_DBG_VERBOSE("process %s, %s: %s",
            endpoint,
            method == MicroOcpp::Method::GET ? "GET" :
            method == MicroOcpp::Method::POST ? "POST" : "error",
            body);
    
    int status = 500;
    StaticJsonDocument<512> response;
    if (resp_body_size >= sizeof("{}")) {
        sprintf(resp_body, "%s", "{}");
    }

    StaticJsonDocument<512> request;
    if (*body) {
        auto err = deserializeJson(request, body);
        if (err) {
            MO_DBG_WARN("malformatted body: %s", err.c_str());
            return 400;
        }
    }
    
    unsigned int connectorId = 0;

    if (strlen(endpoint) >= 11) {
        if (endpoint[11] == '1') {
            connectorId = 1;
        } else if (endpoint[11] == '2') {
            connectorId = 2;
        }
    }

    MO_DBG_VERBOSE("connectorId = %u", connectorId);

    Evse *evse = nullptr;
    if (connectorId >= 1 && connectorId < MO_NUM_EVSEID) {
        evse = &connectors[connectorId];
    }

    //start different api endpoints
    if(str_match(endpoint, "/connectors")) {
        MO_DBG_VERBOSE("query connectors");
        response.add("1");
        response.add("2");
        status = 200;
    } else if(str_match(endpoint, "/connector/*/evse")){
        MO_DBG_VERBOSE("query evse");
        if (!evse) {
            return 404;
        }

        if (method == MicroOcpp::Method::POST) {
            if (request.containsKey("evPlugged")) {
                evse->setEvPlugged(request["evPlugged"]);
                evse->setEvsePlugged(request["evPlugged"]);
            }
            if (request.containsKey("evsePlugged")) {
            evse->setEvsePlugged(request["evsePlugged"]);
            }
            if (request.containsKey("evReady")) {
                evse->setEvReady(request["evReady"]);
            }
            if (request.containsKey("evseReady")) {
                evse->setEvseReady(request["evseReady"]);
            }
        }

        response["evPlugged"] = evse->getEvPlugged();
        response["evsePlugged"] = evse->getEvsePlugged();
        response["evReady"] = evse->getEvReady();
        response["evseReady"] = evse->getEvseReady();
        response["chargePointStatus"] = evse->getOcppStatus();
        status = 200;
    } else if(str_match(endpoint, "/connector/*/meter")){
        MO_DBG_VERBOSE("query meter");
        if (!evse) {
            return 404;
        }

        response["energy"] = evse->getEnergy();
        response["power"] = evse->getPower();
        response["current"] = evse->getCurrent();
        response["voltage"] = evse->getVoltage();
        status = 200;
    } else if(str_match(endpoint, "/connector/*/transaction")){
        MO_DBG_VERBOSE("query transaction");
        if (!evse) {
            return 404;
        }

        if (method == MicroOcpp::Method::POST) {
            if (request.containsKey("idTag")) {
                evse->presentNfcTag(request["idTag"] | "");
            }
        }
        response["idTag"] = evse->getSessionIdTag();
        response["transactionId"] = (char*)evse->getTransactionId().c_str(); //force copy
        response["authorizationStatus"] = "";
        status = 200;
    } else if(str_match(endpoint, "/connector/*/smartcharging")){
        MO_DBG_VERBOSE("query smartcharging");
        if (!evse) {
            return 404;
        }

        response["maxPower"] = evse->getSmartChargingMaxPower();
        response["maxCurrent"] = evse->getSmartChargingMaxCurrent();
        status = 200;
    } else {
        return 404;
    }

    if (response.overflowed()) {
        return 500;
    }

    std::string out;
    serializeJson(response, out);
    if (out.length() >= resp_body_size) {
        return 500;
    }

    if (!out.empty()) {
        sprintf(resp_body, "%s", out.c_str());
    }

    return status;
}

int mocpp_api2_call(const char *uri_raw, size_t uri_raw_len, MicroOcpp::Method method, const char *query_raw, size_t query_raw_len, char *resp_body, size_t resp_body_size) {

    snprintf(resp_body, resp_body_size, "%s", "");
    
    struct mg_str uri = mg_str_n(uri_raw, uri_raw_len);
    struct mg_str query = mg_str_n(query_raw, query_raw_len);

    int evse_id = -1;
    int connector_id = -1;

    unsigned int num;
    struct mg_str evse_id_str = mg_http_var(query, mg_str("evse_id"));
    if (evse_id_str.buf) {
        if (!mg_str_to_num(evse_id_str, 10, &num, sizeof(num)) || num < 1 || num >= MO_NUM_EVSEID) {
            snprintf(resp_body, resp_body_size, "invalid connector_id");
            return 400;
        }
        evse_id = (int)num;
    }

    struct mg_str connector_id_str = mg_http_var(query, mg_str("connector_id"));
    if (connector_id_str.buf) {
        if (!mg_str_to_num(connector_id_str, 10, &num, sizeof(num)) || num != 1) {
            snprintf(resp_body, resp_body_size, "invalid connector_id");
            return 400;
        }
        connector_id = (int)num;
    }

    if (mg_match(uri, mg_str("/plugin"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        if (evse_id <= 0) {
            snprintf(resp_body, resp_body_size, "no action taken");
            return 200;
        } else {
            snprintf(resp_body, resp_body_size, "%s", connectors[evse_id].getEvPlugged() ? "EV already plugged" : "plugged in EV");
            connectors[evse_id].setEvPlugged(true);
            connectors[evse_id].setEvReady(true);
            connectors[evse_id].setEvseReady(true);
            return 200;
        }
    } else if (mg_match(uri, mg_str("/plugout"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        if (evse_id <= 0) {
            snprintf(resp_body, resp_body_size, "no action taken");
            return 200;
        } else {
            snprintf(resp_body, resp_body_size, "%s", connectors[evse_id].getEvPlugged() ? "EV already unplugged" : "unplug EV");
            connectors[evse_id].setEvPlugged(false);
            connectors[evse_id].setEvReady(false);
            connectors[evse_id].setEvseReady(false);
            return 200;
        }
    } else if (mg_match(uri, mg_str("/end"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        bool trackEvReady = false;
        for (size_t i = 1; i < connectors.size(); i++) {
            trackEvReady |= connectors[i].getEvReady();
            connectors[i].setEvReady(false);
        }
        snprintf(resp_body, resp_body_size, "%s", trackEvReady ? "suspended EV" : "EV already suspended");
        return 200;
    } else if (mg_match(uri, mg_str("/state"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        struct mg_str ready_str = mg_http_var(query, mg_str("ready"));
        bool ready = true;
        if (ready_str.buf) {
            if (mg_match(ready_str, mg_str("true"), NULL)) {
                ready = true;
            } else if (mg_match(ready_str, mg_str("false"), NULL)) {
                ready = false;
            } else {
                snprintf(resp_body, resp_body_size, "invalid ready");
                return 400;
            }
        }
        bool trackEvReady = false;
        for (size_t i = 1; i < connectors.size(); i++) {
            if (connectors[i].getEvPlugged()) {
                bool trackEvReady = connectors[i].getEvReady();
                connectors[i].setEvReady(ready);
                snprintf(resp_body, resp_body_size, "%s, %s", ready ? "EV suspended" : "EV not suspended", trackEvReady ? "suspended before" : "not suspended before");
                return 200;
            }
        }
        snprintf(resp_body, resp_body_size, "no action taken - EV not plugged");
        return 200;
    } else if (mg_match(uri, mg_str("/authorize"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        struct mg_str id = mg_http_var(query, mg_str("id"));
        if (!id.buf) {
            snprintf(resp_body, resp_body_size, "missing id");
            return 400;
        }
        struct mg_str type = mg_http_var(query, mg_str("type"));
        if (!id.buf) {
            snprintf(resp_body, resp_body_size, "missing type");
            return 400;
        }

        #if MO_ENABLE_V201
        #define ID_LEN_MAX MO_IDTOKEN_LEN_MAX
        #elif MO_ENABLE_V16
        #define ID_LEN_MAX MO_IDTAG_LEN_MAX
        #endif

        int ret;
        char id_buf [ID_LEN_MAX + 1];
        ret = snprintf(id_buf, sizeof(id_buf), "%.*s", (int)id.len, id.buf);
        if (ret < 0 || ret >= sizeof(id_buf)) {
            snprintf(resp_body, resp_body_size, "invalid id");
            return 400;
        }
        char type_buf [128];
        ret = snprintf(type_buf, sizeof(type_buf), "%.*s", (int)type.len, type.buf);
        if (ret < 0 || ret >= sizeof(type_buf)) {
            snprintf(resp_body, resp_body_size, "invalid type");
            return 400;
        }

        if (evse_id < 0) {
            snprintf(resp_body, resp_body_size, "invalid evse_id");
            return 400;
        }

        bool trackAuthActive = connectors[evse_id].getSessionIdTag();

        if (!connectors[evse_id].presentNfcTag(id_buf, type_buf)) {
            snprintf(resp_body, resp_body_size, "invalid id and / or type");
            return 400;
        }

        bool authActive = connectors[evse_id].getSessionIdTag();

        snprintf(resp_body, resp_body_size, "%s",
                !trackAuthActive && authActive ? "authorize in progress" : 
                trackAuthActive && !authActive ? "unauthorize in progress" : 
                trackAuthActive && authActive ?  "no action taken (EVSE still authorized)" : 
                                                 "no action taken (EVSE not authorized)");

        return 200;
    } else if (mg_match(uri, mg_str("/memory/info"), NULL)) {
        #if MO_OVERRIDE_ALLOCATION && MO_ENABLE_HEAP_PROFILER
        {
            if (method != MicroOcpp::Method::GET) {
                return 405;
            }

            int ret = mo_mem_write_stats_json(resp_body, resp_body_size);
            if (ret < 0 || ret >= resp_body_size) {
                snprintf(resp_body, resp_body_size, "internal error");
                return 500;
            }

            return 200;
        }
        #else
        {
            snprintf(resp_body, resp_body_size, "memory profiler disabled");
            return 404;
        }
        #endif
    } else if (mg_match(uri, mg_str("/memory/reset"), NULL)) {
        #if MO_OVERRIDE_ALLOCATION && MO_ENABLE_HEAP_PROFILER
        {
            if (method != MicroOcpp::Method::POST) {
                return 405;
            }

            MO_MEM_RESET();
            return 200;
        }
        #else
        {
            snprintf(resp_body, resp_body_size, "memory profiler disabled");
            return 404;
        }
        #endif

    }

    return 404;
}

bool mocpp_api3_call(const char *module, const char *operation, const char **params_key, const char **params_val, size_t params_len) {

    int evse_id = -1;
    int connector_id = -1;
    const char *id = nullptr;
    const char *type = nullptr;
    int ready = -1; //boolean interpretation: pos = true, 0 = false, neg = undefined
    int halfway = -1; //bool
    int faulted = -1; //bool
    int unlock_failed = -1; //bool
    int refused_local_auth_list = -1; //bool
    const char *charging_limit = nullptr;
    
    for (size_t i = 0; i < params_len; i++) {

        const char *key = params_key[i];
        const char *val = params_val[i];

        bool val_isnum = true;
        int val_num = 0;
        for (size_t j = 0; val[j] != '\0'; j++) {
            char c = val[j];
            if (j == 0 && c == '-') {
                continue;
            }
            if (c < '0' || c > '9') {
                val_isnum = false;
                break;
            }
            val_num *= 10;
            val_num += c - '0';
        }

        if (val_isnum && val[0] == '0') {
            val_num *= -1;
        }

        bool val_isbool = true;
        bool val_bool = false;
        if (!strcmp(val, "true")) {
            val_bool = true;
        } else if (!strcmp(val, "false")) {
            val_bool = true;
        } else {
            val_isbool = false;
        }

        if (!strcmp(key, "evse_id")) {
            if (!val_isnum || val_num < 0 || val_num >= MO_NUM_EVSEID) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            evse_id = val_num;
        } else if (!strcmp(key, "connector_id")) {
            if (!val_isnum || val_num != 1) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
        } else if (!strcmp(key, "id")) {
            id = val;
        } else if (!strcmp(key, "type")) {
            type = val;
        } else if (!strcmp(key, "ready")) {
            if (!val_isbool) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            ready = val_bool ? 1 : 0;
        } else if (!strcmp(key, "halfway")) {
            if (!val_isbool) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            halfway = val_bool ? 1 : 0;
        } else if (!strcmp(key, "faulted")) {
            if (!val_isbool) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            faulted = val_bool ? 1 : 0;
        } else if (!strcmp(key, "unlock_failed")) {
            if (!val_isbool) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            unlock_failed = val_bool ? 1 : 0;
        } else if (!strcmp(key, "refused_local_auth_list")) {
            if (!val_isbool) {
                MO_DBG_ERR("invalid arg");
                return false;
            }
            refused_local_auth_list = val_bool ? 1 : 0;
        } else if (!strcmp(key, "charging_limit")) {
            charging_limit = val;
        } else {
            MO_DBG_ERR("unknown param: %s", key);
            return false;
        }
    }

    if (!strcmp(operation, "authorize")) {

        #if MO_ENABLE_V201
        #define ID_LEN_MAX MO_IDTOKEN_LEN_MAX
        #elif MO_ENABLE_V16
        #define ID_LEN_MAX MO_IDTAG_LEN_MAX
        #endif

        if (!id || strlen(id) > ID_LEN_MAX) {
            MO_DBG_ERR("invalid arg");
            return false;
        }

        if (evse_id < 0) {
            MO_DBG_ERR("invalid arg");
            return false;
        }

        if (!connectors[evse_id].presentNfcTag(id, type)) {
            MO_DBG_ERR("invalid arg");
            return false;
        }

        MO_DBG_INFO("swiped NFC card");
        return true;
    } else if (!strcmp(module, "ev_emulator")) {

        if (!strcmp(operation, "plugin")) {

            for (size_t i = 1; i < connectors.size(); i++) {
                connectors[i].setEvPlugged(true);
                // connectors[i].setEvReady(true);
                // loop will set EV ready once EVSE ready
            }

            MO_DBG_INFO("plug in cable (EV side)");
            return true;
        } else if (!strcmp(operation, "plugout")) {

            for (size_t i = 1; i < connectors.size(); i++) {
                connectors[1].setEvPlugged(false);
                connectors[1].setEvReady(false);
            }

            MO_DBG_INFO("plug out cable (EV side)");
            return true;
        } else if (!strcmp(operation, "end")) {

            for (size_t i = 1; i < connectors.size(); i++) {
                connectors[i].setEvReady(false);
            }

            MO_DBG_INFO("EV ends charging");
            return true;
        } else if (!strcmp(operation, "state")) {

            if (ready < 0) {
                MO_DBG_ERR("invalid arg");
                return false;
            }

            for (size_t i = 1; i < connectors.size(); i++) {
                connectors[i].setEvReady(ready);
            }

            MO_DBG_INFO("EV %s", ready ? "ready to accept charge" : "not ready to charge");
            return true;
        }
    } else if (!strcmp(module, "sut")) {

        if (!strcmp(operation, "plugin")) {

            if (evse_id <= 0) {
                MO_DBG_ERR("invalid args");
                return false;
            }

            connectors[evse_id].setEvsePlugged(true);
            // connectors[evse_id].setEvseReady(true);
            // loop will set EVSE ready once charging allowed ready

            MO_DBG_INFO("plug in cable (EVSE side)");
            return true;
        } else if (!strcmp(operation, "plugout")) {

            if (evse_id <= 0) {
                MO_DBG_ERR("invalid args");
                return false;
            }

            connectors[evse_id].setEvsePlugged(false);
            connectors[evse_id].setEvseReady(false);

            MO_DBG_INFO("plug out cable (EVSE side)");
            return true;
        } else if (!strcmp(operation, "reboot")) {
            if (!mocpp_api_reboot_cb) {
                MO_DBG_ERR("reboot operation not supported");
                return false;
            }
            mocpp_api_schedule_event(3, [] () {
                mocpp_api_reboot_cb();
            });
            MO_DBG_INFO("triggered reboot");
            return true;
        } else if (!strcmp(operation, "state")) {

            if (faulted >= 0) {
                connectors[evse_id >= 0 ? evse_id : 0].setErrorCode(faulted > 0 ? "InternalError" : nullptr);
            }

            if (charging_limit) {
                float limit_float = -1.f;
                if (*charging_limit) {
                    if (sscanf(charging_limit, "%f", &limit_float) != 1) {
                        MO_DBG_ERR("invalid arg");
                        return false;
                    }

                    if (charging_limit[strlen(charging_limit)-1] == 'A') {
                        limit_float *= 230.f * 3.f;
                    }
                }

                connectors[evse_id >= 0 ? evse_id : 0].setPowerLimit(limit_float);
            }

            MO_DBG_INFO("updated state");
            return true;
        }
    }

    MO_DBG_ERR("unsupported operation");
    return false;
}
