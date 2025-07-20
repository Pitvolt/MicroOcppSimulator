// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include <iostream>
#include <signal.h>

#include <mbedtls/platform.h>

#include <MicroOcpp.h>
#include <MicroOcpp/Context.h>
#include <MicroOcpp/Core/FilesystemUtils.h>
#include "evse.h"
#include "api.h"

#include <MicroOcpp/Core/Memory.h>

#if MO_NUM_EVSEID == 3
std::array<Evse, MO_NUM_EVSEID> connectors {{0, 1, 2}};
#else
std::array<Evse, MO_NUM_EVSEID> connectors {{0, 1}};
#endif

bool g_runSimulator = true;

bool g_isUpAndRunning = false; //if the initial BootNotification and StatusNotifications got through + 1s delay
int32_t g_bootNotificationTime = -1;

#define MO_NETLIB_MONGOOSE 1
#define MO_NETLIB_WASM 2


#if MO_NETLIB == MO_NETLIB_MONGOOSE
#include "mongoose.h"
#include <MicroOcppMongooseClient.h>

#include "net_mongoose.h"

struct mg_mgr mgr;
MO_MG_Connection *g_wsClient;

#elif MO_NETLIB == MO_NETLIB_WASM
#include <emscripten.h>

#include <MicroOcpp/Core/Connection.h>

#include "net_wasm.h"

MicroOcpp::Connection *conn = nullptr;

#else
#error Please ensure that build flag MO_NETLIB is set as MO_NETLIB_MONGOOSE or MO_NETLIB_WASM
#endif

#if MBEDTLS_PLATFORM_MEMORY //configure MbedTLS with allocation hook functions

void *mo_mem_mbedtls_calloc( size_t n, size_t count ) {
    size_t size = n * count;
    auto ptr = MO_MALLOC("MbedTLS", size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}
void mo_mem_mbedtls_free( void *ptr ) {
    MO_FREE(ptr);
}

#endif //MBEDTLS_PLATFORM_MEMORY

void mo_sim_sig_handler(int s){

    if (!g_runSimulator) { //already tried to shut down, now force stop
        exit(EXIT_FAILURE);
    }

    g_runSimulator = false; //shut down simulator gracefully
}

/*
 * Setup MicroOcpp and API
 */
int load_ocpp_version(MO_FilesystemAdapter *filesystem) {

    MicroOcpp::JsonDoc doc (0);
    auto loadStatus = MicroOcpp::FilesystemUtils::loadJson(filesystem, SIMULATOR_FN, doc, "Simulator");
    switch (loadStatus) {
        case MicroOcpp::FilesystemUtils::LoadStatus::Success:
            break; //continue loading JSON
        case MicroOcpp::FilesystemUtils::LoadStatus::FileNotFound:
            break; //file does not exist yet - create file and use default value
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOOM:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrFileCorruption:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOther:
            printf("[Sim] failed to load %s\n", SIMULATOR_FN);
            break; //error - create new file and use default value
    }

    int ocppVersion = -1;

    const char *ocppVersionStr = doc["ocppVersion"] | "_Undefined";
    if (!strcmp(ocppVersionStr, "ocpp1.6") && MO_ENABLE_V16) {
        ocppVersion = MO_OCPP_V16;
    } else if (!strcmp(ocppVersionStr, "ocpp2.0.1") && MO_ENABLE_V201) {
        ocppVersion = MO_OCPP_V201;
    } else {
        ocppVersion = MO_ENABLE_V16 ? MO_OCPP_V16 : MO_OCPP_V201;
    }

    //write back doc to initialize ocppVersion field
    MicroOcpp::JsonDoc doc2 = MicroOcpp::initJsonDoc("Simulator", doc.as<JsonObject>().memoryUsage() + 256);
    doc2 = doc.as<JsonObject>();
    doc2["ocppVersion"] = ocppVersion == MO_OCPP_V16 ? "ocpp1.6" : "ocpp2.0.1";
    auto storeStatus = MicroOcpp::FilesystemUtils::storeJson(filesystem, SIMULATOR_FN, doc2);
    if (storeStatus != MicroOcpp::FilesystemUtils::StoreStatus::Success) {
        printf("[Sim] store error: %s\n", SIMULATOR_FN);
    }

    return ocppVersion;
}

#if MO_NETLIB == MO_NETLIB_MONGOOSE

#ifndef MO_SIM_ENDPOINT_URL
#define MO_SIM_ENDPOINT_URL "http://0.0.0.0:8000" //URL to forward to mg_http_listen(). Will be ignored if the URL field exists in api.jsn
#endif

int main() {

    setbuf(stdout, NULL); //disable buffered printing

#if MBEDTLS_PLATFORM_MEMORY
    mbedtls_platform_set_calloc_free(mo_mem_mbedtls_calloc, mo_mem_mbedtls_free);
#endif //MBEDTLS_PLATFORM_MEMORY

    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = mo_sim_sig_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    mg_log_set(MG_LL_INFO);                            
    mg_mgr_init(&mgr);

    //Begin MO lifecycle
    mo_initialize();

    //Set filesystem config. After that, it is possible to use the internal MO file abstraction layer
    mo_setFilesystemConfig(MO_FS_OPT_USE_MOUNT);
    auto filesystem = mo_getFilesystem();

    /*
     * Setup Simulator web API: listen to HTTP requests which tell the Simulator into which state to go
     */

    struct mg_str api_cert = mg_file_read(&mg_fs_posix, MO_FILENAME_PREFIX "api_cert.pem");
    struct mg_str api_key = mg_file_read(&mg_fs_posix, MO_FILENAME_PREFIX "api_key.pem");

    MicroOcpp::JsonDoc api_settings_doc (0);
    auto loadStatus = MicroOcpp::FilesystemUtils::loadJson(filesystem, "api.jsn", api_settings_doc, "Simulator");
    switch (loadStatus) {
        case MicroOcpp::FilesystemUtils::LoadStatus::Success:
            break; //continue loading JSON
        case MicroOcpp::FilesystemUtils::LoadStatus::FileNotFound:
            break; //file does not exist yet - create file
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOOM:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrFileCorruption:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOther:
            printf("[Sim] failed to load %s\n", SIMULATOR_FN);
            break; //error - create new file
    }

    JsonObject api_settings = api_settings_doc.as<JsonObject>();

    const char *api_url = api_settings["url"] | MO_SIM_ENDPOINT_URL;

    mg_http_listen(&mgr, api_url, http_serve, (void*)api_url); // Create listening connection

    /*
     * Setup Simulator remote control interface: connect to test driver server and execute PRCs from test driver
     */
    
    struct mg_str rmt_ctrl_ca = mg_file_read(&mg_fs_posix, MO_FILENAME_PREFIX "rmt_ctrl.pem");

    MicroOcpp::JsonDoc rmt_ctrl_settings_doc (0);
    loadStatus = MicroOcpp::FilesystemUtils::loadJson(filesystem, "rmt_ctrl.jsn", rmt_ctrl_settings_doc, "Simulator");
    switch (loadStatus) {
        case MicroOcpp::FilesystemUtils::LoadStatus::Success:
            break; //continue loading JSON
        case MicroOcpp::FilesystemUtils::LoadStatus::FileNotFound:
            break; //file does not exist yet - create file
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOOM:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrFileCorruption:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOther:
            printf("[Sim] failed to load %s\n", SIMULATOR_FN);
            break; //error - create new file
    }

    JsonObject rmt_ctrl_settings = rmt_ctrl_settings_doc.as<JsonObject>();

    const char *rmt_ctrl_url = rmt_ctrl_settings["url"] | "";
    const char *rmt_ctrl_api_token = rmt_ctrl_settings["api_token"] | "";

    rmt_ctrl_initialize(&mgr, rmt_ctrl_url, rmt_ctrl_api_token, rmt_ctrl_ca.buf);

    //write file back (creates file if running for the first time)
    MicroOcpp::JsonDoc rmt_ctrl_settings_doc2 = MicroOcpp::initJsonDoc("Simulator", rmt_ctrl_settings_doc.as<JsonObject>().memoryUsage() + 256);
    rmt_ctrl_settings_doc2 = rmt_ctrl_settings_doc.as<JsonObject>();
    JsonObject rmt_ctrl_settings2 = rmt_ctrl_settings_doc2.to<JsonObject>();
    rmt_ctrl_settings2["url"] = rmt_ctrl_url;
    rmt_ctrl_settings2["api_token"] = rmt_ctrl_api_token;
    auto storeStatus = MicroOcpp::FilesystemUtils::storeJson(filesystem, "rmt_ctrl.jsn", rmt_ctrl_settings_doc2);
    if (storeStatus != MicroOcpp::FilesystemUtils::StoreStatus::Success) {
        printf("[Sim] store error: %s\n", SIMULATOR_FN);
    }

    /*
     * Setup MO
     */

    //Load OCPP version from config file (not part of MO) and setup MO with it
    int ocppVersion = load_ocpp_version(filesystem);
    mo_setOcppVersion(ocppVersion);

    g_wsClient = mo_createMongooseWsClient(
        mo_getApiContext(),
        filesystem,
        &mgr,
        "ws://echo.websocket.events",
        "charger-01",
        "",
        "");

    server_initialize(g_wsClient, api_cert.buf ? api_cert.buf : "", api_key.buf ? api_key.buf : "", api_settings["user"] | "", api_settings["pass"] | "");

    //write file back (creates file if running for the first time)
    MicroOcpp::JsonDoc api_settings_doc2 = MicroOcpp::initJsonDoc("Simulator", api_settings_doc.as<JsonObject>().memoryUsage() + 256);
    api_settings_doc2 = api_settings_doc.as<JsonObject>();
    JsonObject api_settings2 = api_settings_doc2.to<JsonObject>();
    api_settings2["url"] = api_url;
    api_settings2["user"] = api_settings["user"] | "";
    api_settings2["pass"] = api_settings["pass"] | "";
    storeStatus = MicroOcpp::FilesystemUtils::storeJson(filesystem, "api.jsn", api_settings_doc2);
    if (storeStatus != MicroOcpp::FilesystemUtils::StoreStatus::Success) {
        printf("[Sim] store error: %s\n", SIMULATOR_FN);
    }

    //set data to send in BootNotification
    mo_setBootNotificationData("MicroOcpp Simulator", "MicroOcpp");

    //Simulator sets up further MO config
    for (unsigned int i = 0; i < connectors.size(); i++) {
        connectors[i].setup(mo_getApiContext(), filesystem);
    }

    mo_setOnResetExecute([] () {
        g_runSimulator = false;
    });

    mocpp_api_set_reboot_cb([] () {
        g_runSimulator = false;
    });

    MO_FTPConfig ftpConfig;
    memset(&ftpConfig, 0, sizeof(ftpConfig));
    ftpConfig.tls_only = true;
    mo_setFtpConfig2(mo_getApiContext(), ftpConfig);

    //Finalize MO setup. Now, the configuration of MO cannot be changed anymore
    mo_setup();

    while (g_runSimulator) { //Run Simulator until OCPP Reset is executed or user presses Ctrl+C
        mg_mgr_poll(&mgr, 100);
        rmt_ctrl_loop();
        mo_loop();

        for (unsigned int i = 0; i < connectors.size(); i++) {
            connectors[i].loop();
        }

        if (!g_bootNotificationTime < 0 && mo_getContext()->getClock().getUptime().isUnixTime()) {
            //time has been set, BootNotification succeeded
            g_bootNotificationTime = mo_getContext()->getClock().getUptimeInt();
        }

        if (!g_isUpAndRunning && g_bootNotificationTime >= 0 && mo_getContext()->getClock().getUptimeInt() - g_bootNotificationTime >= 1) {
            printf("[Sim] Resetting maximum heap usage after boot success\n");
            g_isUpAndRunning = true;
            MO_MEM_RESET();
        }

        mocpp_api_loop();
    }

    printf("[Sim] Shutting down Simulator\n");

    MO_MEM_PRINT_STATS();

    rmt_ctrl_deinitialize();

    mo_deinitialize();
    mo_freeMongooseWsClient(g_wsClient);

    mg_mgr_free(&mgr);
    free(rmt_ctrl_ca.buf);
    free(api_cert.buf);
    free(api_key.buf);
    return 0;
}

#elif MO_NETLIB == MO_NETLIB_WASM

int main() {

    printf("[WASM] start\n");

    auto filesystem = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Deactivate);

    conn = wasm_ocpp_connection_init(nullptr, nullptr, nullptr);

    app_setup(*conn, filesystem);

    const int LOOP_FREQ = 10; //called 10 times per second
    const int BLOCK_INFINITELY = 0; //0 for non-blocking execution, 1 for blocking infinitely
    emscripten_set_main_loop(app_loop, LOOP_FREQ, BLOCK_INFINITELY);

    printf("[WASM] setup complete\n");
}
#endif
