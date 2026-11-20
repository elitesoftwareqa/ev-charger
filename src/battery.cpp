#include "battery.h"

#include <algorithm>
#include <stdexcept>

#include "constants.h"

Battery::Battery(double capacity_kwh, double initial_soc_percent)
    : capacity_kwh_(capacity_kwh), energy_kwh_(0.0) {
    if (capacity_kwh <= 0) {
        throw std::invalid_argument("capacity must be positive");
    }
    if (initial_soc_percent < constants::kPercentEmpty ||
        initial_soc_percent > constants::kPercentFull) {
        throw std::invalid_argument("state of charge must be between 0 and 100");
    }
    energy_kwh_ = capacity_kwh * initial_soc_percent / constants::kPercentFull;
}

double Battery::capacityKwh() const { return capacity_kwh_; }

double Battery::energyKwh() const { return energy_kwh_; }

double Battery::stateOfChargePercent() const {
    return energy_kwh_ / capacity_kwh_ * constants::kPercentFull;
}

bool Battery::isFull() const { return energy_kwh_ >= capacity_kwh_; }

ChargeLevel Battery::chargeLevel() const {
    if (isFull()) return ChargeLevel::Full;
    if (energy_kwh_ <= 0.0) return ChargeLevel::Empty;
    if (stateOfChargePercent() < constants::kLowChargePercent) return ChargeLevel::Low;
    return ChargeLevel::Normal;
}

double Battery::addEnergy(double kwh) {
    if (kwh < 0) {
        throw std::invalid_argument("energy must not be negative");
    }
    const double accepted = std::min(kwh, capacity_kwh_ - energy_kwh_);
    energy_kwh_ += accepted;
    return accepted;
}

double Battery::hoursToFull(double power_kw) const {
    if (power_kw <= 0) {
        throw std::invalid_argument("power must be positive");
    }
    return (capacity_kwh_ - energy_kwh_) / power_kw;
}
