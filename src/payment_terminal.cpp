#include "payment_terminal.h"

#include <stdexcept>

PaymentTerminal::PaymentTerminal(double card_balance) : card_balance_(card_balance) {
    if (card_balance < 0) {
        throw std::invalid_argument("card balance must not be negative");
    }
}

bool PaymentTerminal::authorize(double amount) {
    if (amount <= 0) {
        throw std::invalid_argument("authorization amount must be positive");
    }
    if (offline_ || isAuthorized() || amount > card_balance_) return false;
    authorized_ = amount;
    return true;
}

bool PaymentTerminal::capture(double amount) {
    if (amount < 0) {
        throw std::invalid_argument("capture amount must not be negative");
    }
    if (offline_ || !isAuthorized() || amount > authorized_) return false;
    card_balance_ -= amount;
    authorized_ = 0.0;
    return true;
}

void PaymentTerminal::release() { authorized_ = 0.0; }

void PaymentTerminal::setOffline(bool offline) { offline_ = offline; }

bool PaymentTerminal::isOffline() const { return offline_; }

bool PaymentTerminal::isAuthorized() const { return authorized_ > 0.0; }

double PaymentTerminal::authorizedAmount() const { return authorized_; }

double PaymentTerminal::cardBalance() const { return card_balance_; }
