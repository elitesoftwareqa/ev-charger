#include "charging_app.h"

#include <stdexcept>

#include "battery.h"
#include "charger.h"
#include "payment_terminal.h"

ChargingApp::ChargingApp(Charger& charger, Battery& battery, PaymentTerminal& payment_method)
    : charger_(charger), battery_(battery), session_(payment_method, charger, battery) {}

void ChargingApp::connect() { connected_ = true; }

void ChargingApp::disconnect() { connected_ = false; }

bool ChargingApp::isConnected() const { return connected_; }

StartResult ChargingApp::startCharge(double budget) {
    if (budget <= 0) {
        throw std::invalid_argument("budget must be positive");
    }
    if (!connected_) return StartResult::NotConnected;
    if (session_.active()) return StartResult::AlreadyCharging;
    if (charger_.state() != ChargerState::Available) return StartResult::ChargerUnavailable;

    if (!session_.start(budget)) {
        events_.push_back(AppEvent::StartDeclined);
        return StartResult::PaymentDeclined;
    }
    budget_notified_ = false;
    full_notified_ = false;
    events_.push_back(AppEvent::ChargingStarted);
    return StartResult::Started;
}

StopResult ChargingApp::stopCharge() {
    if (!connected_) return {StopStatus::NotConnected, {}};
    if (!session_.active()) return {StopStatus::NoActiveSession, {}};

    const Receipt receipt = session_.finish();
    last_receipt_ = receipt;
    events_.push_back(AppEvent::ChargingStopped);
    return {StopStatus::Stopped, receipt};
}

void ChargingApp::advance(double seconds, double car_requested_kw) {
    if (!session_.active()) return;
    session_.charge(seconds, car_requested_kw);

    // Tell the driver why charging ended on its own (once each).
    if (!budget_notified_ && session_.budgetExhausted()) {
        budget_notified_ = true;
        events_.push_back(AppEvent::BudgetReached);
    } else if (!full_notified_ && !budget_notified_ && battery_.isFull()) {
        full_notified_ = true;
        events_.push_back(AppEvent::BatteryFull);
    }
}

AppStatus ChargingApp::status() const {
    AppStatus s;
    s.connected = connected_;
    s.session_active = session_.active();
    s.charging = charger_.state() == ChargerState::Charging;
    s.battery_percent = battery_.stateOfChargePercent();
    s.energy_delivered_kwh = charger_.energyDeliveredKwh();
    s.cost_so_far = charger_.sessionCost();
    return s;
}

const std::vector<AppEvent>& ChargingApp::events() const { return events_; }

const std::optional<Receipt>& ChargingApp::lastReceipt() const { return last_receipt_; }
