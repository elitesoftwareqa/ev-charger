#pragma once

#include "charger.h"
#include "charger_spec.h"
#include "constants.h"

// Simulated ABB-style DC fast charger (all values are simulated):
//   - no session fee
//   - power tapers once the car battery is above a charge threshold
// The default constructor uses the built-in values in constants.h; the spec
// constructor takes everything from a database row (see charger_database.h).
// The taper is decided from the battery level at the start of each charge()
// call, so call charge() in small time steps for an accurate result.
class AbbCharger : public Charger {
public:
    explicit AbbCharger(double price_per_kwh = constants::kAbbDefaultPricePerKwh);

    // Throws std::invalid_argument if spec.type is not "abb" or the taper
    // values are out of range.
    explicit AbbCharger(const ChargerSpec& spec);

    double deliveredPowerKw(double requested_kw) const override;

private:
    double taper_above_percent_;
    double taper_factor_;
};
