#pragma once

#include <string>
#include <vector>

#include "constants.h"

// One row of the `chargers` table: everything that describes a charger model.
// Fields a given type does not use (for example the taper settings of a BTC
// charger) are ignored when the charger is built.
struct ChargerSpec {
    std::string name;  // unique, e.g. "ABB"
    std::string type;  // "generic", "abb" or "btc" (see constants.h)
    double max_power_kw = 0.0;
    double price_per_kwh = 0.0;
    double session_fee = constants::kNoSessionFee;
    double taper_above_percent = constants::kNoTaperAbovePercent;
    double taper_factor = constants::kNoTaperFactor;
};

// The chargers inserted by ChargerDatabase::seedDefaults(), built from constants.h.
std::vector<ChargerSpec> defaultChargerSpecs();
