#include "payment_session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "battery.h"
#include "charger.h"
#include "constants.h"
#include "payment_terminal.h"

PaymentSession::PaymentSession(PaymentTerminal& terminal, Charger& charger, Battery& battery)
    : terminal_(terminal), charger_(charger), battery_(battery) {}

bool PaymentSession::start(double preauth_amount) {
    if (preauth_amount <= 0) {
        throw std::invalid_argument("pre-authorization amount must be positive");
    }
    if (active_) return false;
    if (!terminal_.authorize(preauth_amount)) return false;

    if (!charger_.plugIn(battery_) || !charger_.startCharging()) {
        charger_.unplug();       // undo a plug-in if only the second step failed
        terminal_.release();     // never leave a hold behind
        return false;
    }

    energy_before_kwh_ = battery_.energyKwh();  // the "before" reading
    budget_exhausted_ = false;
    active_ = true;
    return true;
}

void PaymentSession::charge(double seconds, double requested_kw) {
    if (!active_) return;
    if (seconds < 0 || requested_kw < 0) {
        throw std::invalid_argument("seconds and requested power must not be negative");
    }
    if (charger_.state() != ChargerState::Charging) return;

    const double power_kw = charger_.deliveredPowerKw(requested_kw);
    if (power_kw <= 0) return;

    const double remaining_money = terminal_.authorizedAmount() - charger_.sessionCost();
    const double affordable_kwh = std::max(0.0, remaining_money) / charger_.pricePerKwh();
    const double wanted_kwh = power_kw * seconds / constants::kSecondsPerHour;

    if (wanted_kwh > affordable_kwh) {
        // Only charge for as long as the hold can pay for, then stop.
        charger_.charge(affordable_kwh * constants::kSecondsPerHour / power_kw, requested_kw);
        // If the battery filled first the charger already finished by itself,
        // so the money did not run out.
        if (charger_.stopCharging()) budget_exhausted_ = true;
    } else {
        charger_.charge(seconds, requested_kw);
    }
}

Receipt PaymentSession::finish() {
    if (!active_) throw std::logic_error("no active session");

    if (charger_.state() == ChargerState::Charging) charger_.stopCharging();

    Receipt receipt;
    receipt.energy_before_kwh = energy_before_kwh_;
    receipt.energy_after_kwh = battery_.energyKwh();  // the "after" reading
    receipt.energy_delivered_kwh = charger_.energyDeliveredKwh();
    receipt.energy_matches =
        std::fabs((receipt.energy_after_kwh - receipt.energy_before_kwh) -
                  receipt.energy_delivered_kwh) < constants::kEnergyMatchToleranceKwh;

    const double hold = terminal_.authorizedAmount();
    const double cost = std::min(charger_.sessionCost(), hold);  // never bill above the hold

    if (cost > 0 && terminal_.capture(cost)) {
        receipt.paid = true;
        receipt.amount_charged = cost;
        receipt.amount_released = hold - cost;
    } else {
        receipt.paid = (cost == 0);  // nothing to pay counts as settled
        receipt.amount_released = hold;
    }
    terminal_.release();  // drop whatever is left of the hold

    if (charger_.state() == ChargerState::Faulted) {
        charger_.clearFault();
    } else {
        charger_.unplug();
    }
    active_ = false;
    return receipt;
}

bool PaymentSession::active() const { return active_; }

bool PaymentSession::budgetExhausted() const { return budget_exhausted_; }
