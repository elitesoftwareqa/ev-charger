#pragma once

#include <optional>
#include <vector>

#include "payment_session.h"

class Battery;
class Charger;
class PaymentTerminal;

// Why a start request did or did not go through.
enum class StartResult {
    Started,
    NotConnected,        // the phone has no connection to the charger
    AlreadyCharging,     // this app already has a running session
    ChargerUnavailable,  // charger is occupied or faulted
    PaymentDeclined      // card declined, or not enough balance
};

enum class StopStatus {
    Stopped,
    NotConnected,    // the stop command could not reach the charger
    NoActiveSession
};

struct StopResult {
    StopStatus status;
    Receipt receipt;  // only meaningful when status == Stopped
};

// Things the app would show the driver as notifications.
enum class AppEvent {
    ChargingStarted,
    StartDeclined,
    BudgetReached,    // the pre-authorized amount ran out and charging stopped
    BatteryFull,      // the car is full and charging stopped
    ChargingStopped   // the driver stopped it (or tapped "finish" after it ended)
};

// A snapshot of what the app screen would show.
struct AppStatus {
    bool connected = false;
    bool session_active = false;
    bool charging = false;            // energy is flowing right now
    double battery_percent = 0.0;
    double energy_delivered_kwh = 0.0;
    double cost_so_far = 0.0;
};

// Simulates a driver's phone app that starts and stops a charge remotely.
// The app owns a payment session; the charger, battery and payment method
// (the card on file) must outlive it.
//
// The app only *sends commands*. The charger keeps running on its own, so if
// the phone loses its connection a running charge continues until the budget
// runs out, the battery is full, or the app reconnects and stops it.
class ChargingApp {
public:
    ChargingApp(Charger& charger, Battery& battery, PaymentTerminal& payment_method);

    // Network link between the phone and the charger.
    void connect();
    void disconnect();
    bool isConnected() const;

    // "Start" button. `budget` is the most the driver is willing to spend.
    // Throws std::invalid_argument if budget is not positive.
    StartResult startCharge(double budget);

    // "Stop" button. Settles payment and returns the receipt.
    StopResult stopCharge();

    // Simulates time passing while the car charges. Normally the charger
    // reports this itself; here the test (or demo) drives the clock.
    void advance(double seconds, double car_requested_kw);

    AppStatus status() const;
    const std::vector<AppEvent>& events() const;
    const std::optional<Receipt>& lastReceipt() const;

private:
    Charger& charger_;
    Battery& battery_;
    PaymentSession session_;
    bool connected_ = false;
    bool budget_notified_ = false;
    bool full_notified_ = false;
    std::vector<AppEvent> events_;
    std::optional<Receipt> last_receipt_;
};
