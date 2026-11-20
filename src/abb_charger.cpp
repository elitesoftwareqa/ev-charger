#include "abb_charger.h"

#include <algorithm>
#include <stdexcept>

#include "battery.h"
#include "constants.h"

AbbCharger::AbbCharger(double price_per_kwh)
    : Charger(constants::kAbbMaxPowerKw, price_per_kwh),
      taper_above_percent_(constants::kAbbTaperAbovePercent),
      taper_factor_(constants::kAbbTaperFactor) {}

AbbCharger::AbbCharger(const ChargerSpec& spec)
    : Charger(spec.max_power_kw, spec.price_per_kwh),
      taper_above_percent_(spec.taper_above_percent),
      taper_factor_(spec.taper_factor) {
    if (spec.type != constants::kChargerTypeAbb) {
        throw std::invalid_argument("spec '" + spec.name + "' is not an ABB charger");
    }
    if (taper_factor_ <= 0 || taper_factor_ > 1) {
        throw std::invalid_argument("taper factor must be in (0, 1]");
    }
    if (taper_above_percent_ < constants::kPercentEmpty ||
        taper_above_percent_ > constants::kPercentFull) {
        throw std::invalid_argument("taper threshold must be between 0 and 100 percent");
    }
}

double AbbCharger::deliveredPowerKw(double requested_kw) const {
    double power = Charger::deliveredPowerKw(requested_kw);
    const Battery* b = battery();
    if (b && b->stateOfChargePercent() > taper_above_percent_) {
        power = std::min(power, maxPowerKw() * taper_factor_);
    }
    return power;
}
