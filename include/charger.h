#pragma once

class Battery;  // defined in battery.h

enum class ChargerState {
    Available,  // nothing plugged in
    Connected,  // car plugged in, not charging yet
    Charging,   // energy is flowing
    Finished,   // charging stopped, car still plugged in
    Faulted     // error; must be cleared before reuse
};

// A generic charger. Specific hardware types (see abb_charger.h, btc_charger.h)
// derive from it and override the virtual functions below to change how much
// power is delivered and what a session costs.
class Charger {
public:
    virtual ~Charger() = default;

    // Throws std::invalid_argument if either value is not positive.
    Charger(double max_power_kw, double price_per_kwh);

    ChargerState state() const;

    // State transitions. Each returns true if it was allowed, false otherwise.
    bool plugIn();           // Available  -> Connected (no battery attached)

    // Available -> Connected, with a car battery attached. While charging,
    // delivered energy goes into the battery and charging stops by itself
    // when it is full. The Battery must outlive the connection (until
    // unplug() or clearFault()).
    bool plugIn(Battery& battery);
    bool startCharging();    // Connected  -> Charging
    bool stopCharging();     // Charging   -> Finished
    bool unplug();           // Connected/Finished -> Available (session resets)
    void reportFault();      // any state  -> Faulted
    bool clearFault();       // Faulted    -> Available

    // Simulate time passing while the car requests `requested_kw`.
    // Only adds energy while Charging; power is capped at the charger's max.
    // Throws std::invalid_argument for negative inputs.
    void charge(double seconds, double requested_kw);

    double maxPowerKw() const;
    double pricePerKwh() const;
    double energyDeliveredKwh() const;

    // Total cost so far: sessionFee() + energy * price, or 0 before charging begins.
    double sessionCost() const;

    // Power actually delivered right now for a car requesting `requested_kw`.
    // Default: the request, capped at the charger's maximum.
    virtual double deliveredPowerKw(double requested_kw) const;

    // Fixed fee applied once charging begins. Default: none.
    virtual double sessionFee() const;

protected:
    // The battery currently attached, or nullptr.
    const Battery* battery() const;

private:
    double max_power_kw_;
    double price_per_kwh_;
    double energy_kwh_ = 0.0;
    ChargerState state_ = ChargerState::Available;
    Battery* battery_ = nullptr;
    bool session_started_ = false;
};
