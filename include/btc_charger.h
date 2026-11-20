#pragma once

#include "charger.h"
#include "charger_spec.h"
#include "constants.h"

// Simulated BTC-style charger (all values are simulated):
//   - flat power (no taper)
//   - fixed session fee, applied as soon as charging starts
// The default constructor uses the built-in values in constants.h; the spec
// constructor takes everything from a database row (see charger_database.h).
class BtcCharger : public Charger {
public:
    explicit BtcCharger(double price_per_kwh = constants::kBtcDefaultPricePerKwh);

    // Throws std::invalid_argument if spec.type is not "btc" or the fee is negative.
    explicit BtcCharger(const ChargerSpec& spec);

    double sessionFee() const override;

private:
    double session_fee_;
};
