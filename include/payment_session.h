#pragma once

class Battery;
class Charger;
class PaymentTerminal;

// What the driver gets at the end of a paid session.
struct Receipt {
    double energy_before_kwh = 0.0;     // battery energy when the session started
    double energy_after_kwh = 0.0;      // battery energy when it ended
    double energy_delivered_kwh = 0.0;  // what the charger says it delivered
    double amount_charged = 0.0;        // money actually taken from the card
    double amount_released = 0.0;       // part of the hold given back
    bool paid = false;                  // false if the payment could not be captured
    bool energy_matches = false;        // battery gain == charger-reported energy
};

// Ties a payment terminal, a charger and a car battery into one paid session:
//   start()  -> pre-authorize, plug in, begin charging, record energy "before"
//   charge() -> deliver energy, never spending more than the pre-authorized amount
//   finish() -> stop, record energy "after", capture the real cost, release the rest
// The three objects must outlive the session.
class PaymentSession {
public:
    PaymentSession(PaymentTerminal& terminal, Charger& charger, Battery& battery);

    // Returns false (and changes nothing) if the payment is declined,
    // a session is already active, or the charger is not available.
    // Throws std::invalid_argument if preauth_amount is not positive.
    bool start(double preauth_amount);

    // Delivers energy for `seconds` at the car's requested power.
    // Stops charging by itself once the pre-authorized amount is used up.
    void charge(double seconds, double requested_kw);

    // Ends the session and settles payment.
    // Throws std::logic_error if no session is active.
    Receipt finish();

    bool active() const;
    bool budgetExhausted() const;  // true if charging stopped because the hold ran out

private:
    PaymentTerminal& terminal_;
    Charger& charger_;
    Battery& battery_;
    bool active_ = false;
    bool budget_exhausted_ = false;
    double energy_before_kwh_ = 0.0;
};
