#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "battery.h"
#include "charger.h"
#include "charger_repository.h"
#include "charging_app.h"
#include "payment_terminal.h"

namespace httplib {
class Server;
}

// A JSON-over-HTTP interface to the charger library.
//
//   GET    /health
//   GET    /chargers                         list every charger model + whether it is in use
//   GET    /chargers/{name}                  one charger model
//   POST   /chargers/{name}/session          start a paid charge from the app
//   GET    /chargers/{name}/session          what the app screen shows
//   POST   /chargers/{name}/session/advance  move the simulated clock forward
//   DELETE /chargers/{name}/session          stop, settle payment, return the receipt
//
// Each charger can have one session at a time. Everything is simulated: the
// "car" and "card" are created from the start request. All handlers run under
// one mutex, so the single SQLite connection is never used from two threads.
class RestApi {
public:
    explicit RestApi(ChargerRepository& repository);
    ~RestApi();

    // Adds all routes to `server`. The RestApi must outlive the server.
    void registerRoutes(httplib::Server& server);

private:
    // Everything one running session needs; the app refers to the other members.
    struct Session {
        Session(std::unique_ptr<Charger> c, double capacity_kwh, double start_percent,
                double card_balance)
            : charger(std::move(c)),
              battery(capacity_kwh, start_percent),
              terminal(card_balance),
              app(*charger, battery, terminal) {}
        std::unique_ptr<Charger> charger;
        Battery battery;
        PaymentTerminal terminal;
        ChargingApp app;
    };

    ChargerRepository& repository_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;  // keyed by charger name

    friend struct RestApiHandlers;
};
