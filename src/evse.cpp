// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "evse.h"
#include <MicroOcpp.h>
#include <MicroOcpp/Context.h>
#include <MicroOcpp/Core/FilesystemUtils.h>
#include <MicroOcpp/Model/Model.h>
#include <MicroOcpp/Model/Transactions/TransactionService16.h>
#include <MicroOcpp/Model/Transactions/TransactionService201.h>
#include <MicroOcpp/Model/Variables/VariableService.h>
#include <MicroOcpp/Model/Authorization/IdToken.h>
#include <MicroOcpp/Operations/StatusNotification.h>
#include <MicroOcpp/Version.h>
#include <MicroOcpp/Debug.h>
#include <cstring>
#include <cstdlib>

Evse::Evse(unsigned int connectorId) : connectorId{connectorId} {

}

void Evse::setup(MO_Context *ctx, MO_FilesystemAdapter *filesystem) {

    this->ctx = ctx;
    this->filesystem = filesystem;

    loadLocalState();

    storeLocalState(); //populate JSON file if running the first time

    mo_setConnectorPluggedInput2(ctx, connectorId, [] (unsigned int, void *userData) -> bool {
        auto evse = reinterpret_cast<Evse*>(userData);
        return evse->trackEvPlugged; //return if J1772 is in State B or C
    }, this);

    mo_setEvReadyInput2(ctx, connectorId, [] (unsigned int, void *userData) -> bool {
        auto evse = reinterpret_cast<Evse*>(userData);
        return evse->trackEvReady; //return if J1772 is in State C
    }, this);

    mo_setEvseReadyInput2(ctx, connectorId, [] (unsigned int, void *userData) -> bool {
        auto evse = reinterpret_cast<Evse*>(userData);
        return evse->trackEvseReady;
    }, this);

    mo_setEnergyMeterInput2(ctx, connectorId, [] (MO_ReadingContext, unsigned int, void *userData) -> int32_t {
        auto evse = reinterpret_cast<Evse*>(userData);
        return evse->simulate_energy;
    }, this);

    mo_setPowerMeterInput2(ctx, connectorId, [] (MO_ReadingContext, unsigned int, void *userData) -> float {
        auto evse = reinterpret_cast<Evse*>(userData);
        return evse->simulate_power;
    }, this);

    mo_addMeterValueInputFloat2(ctx, connectorId, [] (MO_ReadingContext, unsigned int, void *userData) -> float {
            auto evse = reinterpret_cast<Evse*>(userData);
            return evse->getCurrent();
        }, 
        "Current.Import",
        "A",
        "Outlet",
        nullptr,
        this);
    
    mo_addMeterValueInputFloat2(ctx, connectorId, [] (MO_ReadingContext, unsigned int, void *userData) -> float {
            auto evse = reinterpret_cast<Evse*>(userData);
            return evse->getVoltage();
        }, 
        "Voltage",
        "V",
        nullptr,
        nullptr,
        this);
    
    mo_addMeterValueInputFloat2(ctx, connectorId, [] (MO_ReadingContext, unsigned int, void *userData) -> float {
            auto evse = reinterpret_cast<Evse*>(userData);
            return (evse->simulate_power > 1.f ? 44.f : 0.f);
        }, 
        "SoC",
        nullptr,
        nullptr,
        nullptr,
        this);

    mo_setOnResetExecute([] () {
        exit(0);
    });

    mo_setSmartChargingOutput(ctx, connectorId, [] (MO_ChargeRate limit, unsigned int, void *userData) {
            auto evse = reinterpret_cast<Evse*>(userData);
            if (limit.power >= 0.f) {
                MO_DBG_DEBUG("set limit: %f", limit.power);
                evse->limit_power = limit.power;
            } else {
                // negative value means no limit defined
                evse->limit_power = evse->SIMULATE_POWER_CONST;
            }
        },
        true, //powerSupported
        false, //currentSupported,
        false, //phases3to1Supported
        false, //phaseSwitchingSupported
        this);

}

bool Evse::loadLocalState() {
    
    MicroOcpp::JsonDoc doc (0);
    auto status = MicroOcpp::FilesystemUtils::loadJson(filesystem, SIMULATOR_FN, doc, "Simulator");
    switch (status) {
        case MicroOcpp::FilesystemUtils::LoadStatus::Success:
            break; //continue loading JSON
        case MicroOcpp::FilesystemUtils::LoadStatus::FileNotFound:
            break; //file does not exist yet - use default values
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOOM:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrFileCorruption:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOther:
            MO_DBG_ERR("failed to load %s", SIMULATOR_FN);
            break; //error - use default values
    }

    JsonObject state = doc["evse"][connectorId-1];

    trackEvPlugged = state["evPlugged"] | false;
    trackEvsePlugged = state["evsePlugged"] | false;
    trackEvReady = state["evReady"] | false;
    trackEvseReady = state["evseReady"] | false;

    return true;
}

bool Evse::storeLocalState() {
    
    MicroOcpp::JsonDoc doc (0);
    auto status = MicroOcpp::FilesystemUtils::loadJson(filesystem, SIMULATOR_FN, doc, "Simulator");
    switch (status) {
        case MicroOcpp::FilesystemUtils::LoadStatus::Success:
            break; //continue writing JSON
        case MicroOcpp::FilesystemUtils::LoadStatus::FileNotFound:
            break; //file does not exist yet - create file
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOOM:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrFileCorruption:
        case MicroOcpp::FilesystemUtils::LoadStatus::ErrOther:
            MO_DBG_ERR("failed to load %s", SIMULATOR_FN);
            break; //error - create new file
    }

    MicroOcpp::JsonDoc doc2 = MicroOcpp::initJsonDoc("Simulator", doc.as<JsonObject>().memoryUsage() +  256);
    doc2 = doc.as<JsonObject>();
    doc.clear();

    JsonArray evseJson = doc2["evse"];
    if (evseJson.isNull()) {
        evseJson = doc2.createNestedArray("evse");
    }
    while (evseJson.size() < connectorId) {
        evseJson.createNestedObject();
    }

    JsonObject state = doc2["evse"][connectorId-1];

    state["evPlugged"] = trackEvPlugged;
    state["evsePlugged"] = trackEvsePlugged;
    state["evReady"] = trackEvReady;
    state["evseReady"] = trackEvseReady;

    auto status2 = MicroOcpp::FilesystemUtils::storeJson(filesystem, SIMULATOR_FN, doc2);
    if (status2 != MicroOcpp::FilesystemUtils::StoreStatus::Success) {
        MO_DBG_ERR("store error: %s", SIMULATOR_FN);
        return false;
    }

    return true;
}

void Evse::loop() {

    auto curStatus = mo_getChargePointStatus2(ctx, connectorId);

    if (status.compare(mo_serializeChargePointStatus(curStatus))) {
        status = mo_serializeChargePointStatus(curStatus);
    }

    bool simulate_isCharging = mo_ocppPermitsCharge2(ctx, connectorId) && trackEvPlugged && trackEvsePlugged && trackEvReady && trackEvseReady;

    simulate_isCharging &= limit_power >= 720.f; //minimum charging current is 6A (720W for 120V grids) according to J1772

    if (simulate_isCharging) {
        if (simulate_power >= 1.f) {
            simulate_energy += (float) (mo_getUptime2(ctx) - simulate_energy_track_time) * simulate_power / 3600.f;
        }

        simulate_power = SIMULATE_POWER_CONST;
        simulate_power = std::min(simulate_power, limit_power);
        simulate_power += (((mo_getUptime2(ctx) / 5) * 3483947) % 20000) * 0.001f - 10.f;
        simulate_energy_track_time = mo_getUptime2(ctx);
    } else {
        simulate_power = 0.f;
    }

}

void Evse::presentNfcTag(const char *uid) {
    if (!uid) {
        MO_DBG_ERR("invalid argument");
        return;
    }

    if (mo_isTransactionActive2(ctx, connectorId)) {
        if (!strcmp(uid, mo_getTransactionIdTag2(ctx, connectorId))) {
            mo_endTransaction2(ctx, connectorId, uid, "Local");
        } else {
            MO_DBG_INFO("RFID card denied");
        }
    } else {
        mo_beginTransaction2(ctx, connectorId, uid);
    }
}

bool Evse::presentNfcTag(const char *uid, const char *type) {

    bool res = false;
    
    #if MO_ENABLE_V16
    if (mo_getOcppVersion2(ctx) == MO_OCPP_V16) {
        presentNfcTag(uid);
        res = true;
    }
    #endif //MO_ENABLE_V16
    #if MO_ENABLE_V201
    if (mo_getOcppVersion2(ctx) == MO_OCPP_V201) {
        MicroOcpp::Ocpp201::IdToken idToken;
        if (!idToken.parseCstr(uid, type)) {
            MO_DBG_ERR("could not parse idToken (%s, %s)", uid, type);
            return false;
        }
        res = mo_authorizeTransaction2(ctx, connectorId, idToken.get(), idToken.getType());
    }
    #endif //MO_ENABLE_V201

    return res;
}

void Evse::setEvPlugged(bool plugged) {
    trackEvPlugged = plugged;
    storeLocalState();
}

bool Evse::getEvPlugged() {
    return trackEvPlugged;
}

void Evse::setEvsePlugged(bool plugged) {
    trackEvsePlugged = plugged;
    storeLocalState();
}

bool Evse::getEvsePlugged() {
    return trackEvsePlugged;
}

void Evse::setEvReady(bool ready) {
    trackEvReady = ready;
    storeLocalState();
}

bool Evse::getEvReady() {
    return trackEvReady;
}

void Evse::setEvseReady(bool ready) {
    trackEvseReady = ready;
    storeLocalState();
}

bool Evse::getEvseReady() {
    return trackEvseReady;
}

const char *Evse::getSessionIdTag() {
    return mo_getTransactionIdTag2(ctx, connectorId) ? mo_getTransactionIdTag2(ctx, connectorId) : "";
}

std::string Evse::getTransactionId() {

    std::string res;

    #if MO_ENABLE_V16
    if (mo_getOcppVersion2(ctx) == MO_OCPP_V16) {
        if (mo_v16_getTransactionId2(ctx, connectorId) > 0) {
            char buf [30];
            snprintf(buf, sizeof(buf), "%i", mo_v16_getTransactionId2(ctx, connectorId));
            res = buf;
        }
    }
    #endif //MO_ENABLE_V16
    #if MO_ENABLE_V201
    if (mo_getOcppVersion2(ctx) == MO_OCPP_V201) {
        res = mo_v201_getTransactionId2(ctx, connectorId) ? mo_v201_getTransactionId2(ctx, connectorId) : "";
    }
    #endif //MO_ENABLE_V201

    return res;
}

bool Evse::chargingPermitted() {
    return mo_ocppPermitsCharge2(ctx, connectorId);
}

int Evse::getPower() {
    return (int) simulate_power;
}

float Evse::getVoltage() {
    if (getPower() > 1.f) {
        return 228.f + (((mo_getUptime2(ctx) / 5) * 7484311) % 4000) * 0.001f;
    } else {
        return 0.f;
    }
}
