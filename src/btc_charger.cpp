#include "btc_charger.h"

#include <stdexcept>

#include "constants.h"

BtcCharger::BtcCharger(double price_per_kwh)
    : Charger(constants::kBtcMaxPowerKw, price_per_kwh),
      session_fee_(constants::kBtcSessionFee) {}

BtcCharger::BtcCharger(const ChargerSpec& spec)
    : Charger(spec.max_power_kw, spec.price_per_kwh), session_fee_(spec.session_fee) {
    if (spec.type != constants::kChargerTypeBtc) {
        throw std::invalid_argument("spec '" + spec.name + "' is not a BTC charger");
    }
    if (session_fee_ < 0) {
        throw std::invalid_argument("session fee must not be negative");
    }
}

double BtcCharger::sessionFee() const { return session_fee_; }
