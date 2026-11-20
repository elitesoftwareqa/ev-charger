#pragma once

// A simulated card payment terminal (the kind mounted on an EV charger).
// Models the usual two-step flow: authorize (hold funds), then capture
// (take the final amount) or release (give the hold back).
// Simulated: this stubs out a real terminal SDK and does not implement any real protocol.
class PaymentTerminal {
public:
    // card_balance: how much money the simulated card can spend.
    explicit PaymentTerminal(double card_balance);

    // Holds `amount` on the card. Returns false (declined) if the terminal is
    // offline, a hold already exists, or the card balance is too low.
    // Throws std::invalid_argument if amount is not positive.
    bool authorize(double amount);

    // Takes `amount` from the card. Returns false if there is no hold, the
    // terminal is offline, or amount exceeds the hold. Clears the hold on success.
    // Throws std::invalid_argument if amount is negative.
    bool capture(double amount);

    // Cancels the current hold (no-op if none).
    void release();

    // Simulate a lost connection.
    void setOffline(bool offline);

    bool isOffline() const;
    bool isAuthorized() const;
    double authorizedAmount() const;
    double cardBalance() const;

private:
    double card_balance_;
    double authorized_ = 0.0;
    bool offline_ = false;
};
