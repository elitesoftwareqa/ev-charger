#pragma once

enum class ChargeLevel {
    Empty,   // 0%
    Low,     // above 0% and below 20%
    Normal,  // 20% up to (not including) 100%
    Full     // 100%
};

class Battery {
public:
    // Throws std::invalid_argument if capacity is not positive or
    // initial_soc_percent is outside 0-100.
    Battery(double capacity_kwh, double initial_soc_percent);

    double capacityKwh() const;
    double energyKwh() const;
    double stateOfChargePercent() const;
    bool isFull() const;

    // Classifies the current state of charge.
    ChargeLevel chargeLevel() const;

    // Adds energy, never exceeding capacity. Returns the kWh actually accepted.
    // Throws std::invalid_argument if kwh is negative.
    double addEnergy(double kwh);

    // Hours needed to reach 100% at a constant power. Returns 0 if already full.
    // Throws std::invalid_argument if power_kw is not positive.
    double hoursToFull(double power_kw) const;

private:
    double capacity_kwh_;
    double energy_kwh_;
};
