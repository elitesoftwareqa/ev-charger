#include "charger.h"

#include "battery.h"

#include <algorithm>
#include <stdexcept>

#include "constants.h"

Charger::Charger(double max_power_kw, double price_per_kwh)
    : max_power_kw_(max_power_kw), price_per_kwh_(price_per_kwh) {
    if (max_power_kw <= 0 || price_per_kwh <= 0) {
        throw std::invalid_argument("max power and price must be positive");
    }
}

ChargerState Charger::state() const { return state_; }

bool Charger::plugIn() {
    if (state_ != ChargerState::Available) return false;
    state_ = ChargerState::Connected;
    return true;
}

bool Charger::plugIn(Battery& battery) {
    if (!plugIn()) return false;
    battery_ = &battery;
    return true;
}

bool Charger::startCharging() {
    if (state_ != ChargerState::Connected) return false;
    state_ = ChargerState::Charging;
    session_started_ = true;
    return true;
}

bool Charger::stopCharging() {
    if (state_ != ChargerState::Charging) return false;
    state_ = ChargerState::Finished;
    return true;
}

bool Charger::unplug() {
    if (state_ != ChargerState::Connected && state_ != ChargerState::Finished) {
        return false;
    }
    state_ = ChargerState::Available;
    energy_kwh_ = 0.0;  // new session next time
    battery_ = nullptr;
    session_started_ = false;
    return true;
}

void Charger::reportFault() { state_ = ChargerState::Faulted; }

bool Charger::clearFault() {
    if (state_ != ChargerState::Faulted) return false;
    state_ = ChargerState::Available;
    energy_kwh_ = 0.0;
    battery_ = nullptr;
    session_started_ = false;
    return true;
}

void Charger::charge(double seconds, double requested_kw) {
    if (seconds < 0 || requested_kw < 0) {
        throw std::invalid_argument("seconds and requested power must not be negative");
    }
    if (state_ != ChargerState::Charging) return;
    const double power_kw = deliveredPowerKw(requested_kw);
    double delivered_kwh = power_kw * seconds / constants::kSecondsPerHour;
    if (battery_) {
        delivered_kwh = battery_->addEnergy(delivered_kwh);  // only what fits
        if (battery_->isFull()) state_ = ChargerState::Finished;
    }
    energy_kwh_ += delivered_kwh;
}

double Charger::maxPowerKw() const { return max_power_kw_; }

double Charger::pricePerKwh() const { return price_per_kwh_; }

double Charger::energyDeliveredKwh() const { return energy_kwh_; }

double Charger::sessionCost() const {
    if (!session_started_) return 0.0;
    return sessionFee() + energy_kwh_ * price_per_kwh_;
}

double Charger::deliveredPowerKw(double requested_kw) const {
    return std::min(requested_kw, max_power_kw_);
}

double Charger::sessionFee() const { return constants::kNoSessionFee; }

const Battery* Charger::battery() const { return battery_; }
