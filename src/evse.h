// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#ifndef EVSE_H
#define EVSE_H

#include <array>
#include <string>

#include <MicroOcpp/Context.h>
#include <MicroOcpp/Core/FilesystemAdapter.h>

#define SIMULATOR_FN "simulator.jsn"

class Evse {
private:
    const unsigned int connectorId;

    MO_Context *ctx = nullptr;
    MO_FilesystemAdapter *filesystem = nullptr;

    bool trackEvPlugged = false;
    bool trackEvsePlugged = false;
    bool trackEvReady = false;
    bool trackEvseReady = false;

    std::string errorCode;

    const float SIMULATE_POWER_CONST = 11000.f;
    float simulate_power = 0;
    float limit_power_ocpp = -1.f;
    float limit_power_api = -1.f;
    const float SIMULATE_ENERGY_DELTA_MS = SIMULATE_POWER_CONST / (3600.f * 1000.f);
    unsigned long simulate_energy_track_time = 0;
    float simulate_energy = 0;

    std::string status;
public:
    Evse(unsigned int connectorId);

    void setup(MO_Context *ctx, MO_FilesystemAdapter *filesystem);

    bool loadLocalState();

    bool storeLocalState();

    void loop();

    void presentNfcTag(const char *uid);

    bool presentNfcTag(const char *uid, const char *type);

    void setEvPlugged(bool plugged);

    bool getEvPlugged();

    void setEvsePlugged(bool plugged);
    
    bool getEvsePlugged();

    void setEvReady(bool ready);

    bool getEvReady();

    void setEvseReady(bool ready);

    bool getEvseReady();

    void setErrorCode(const char *errorCode);

    const char *getErrorCode();

    const char *getSessionIdTag();
    std::string getTransactionId();
    bool chargingPermitted();

    bool isCharging() {
        return chargingPermitted() && trackEvReady;
    }

    const char *getOcppStatus() {
        return status.c_str();
    }

    unsigned int getConnectorId() {
        return connectorId;
    }

    int getEnergy() {
        return (int) simulate_energy;
    }

    int getPower();

    float getVoltage();

    float getCurrent() {
        float volts = getVoltage();
        if (volts <= 0.f) {
            return 0.f;
        }
        return 0.333f * (float) getPower() / volts;
    }

    int getSmartChargingMaxPower() {
        return limit_power_ocpp >= 0.f ? limit_power_ocpp : SIMULATE_POWER_CONST;
    }

    float getSmartChargingMaxCurrent() {
        float volts = getVoltage();
        if (volts <= 0.f) {
            return 0.f;
        }
        return 0.333f * (float) getSmartChargingMaxPower() / volts;
    }

    void setPowerLimit(float limit_api);

};

extern std::array<Evse, MO_NUM_EVSEID> connectors;

#endif
