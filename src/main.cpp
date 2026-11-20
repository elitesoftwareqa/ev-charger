#include <iostream>
#include <string>

#include "battery.h"
#include "charger.h"
#include "charger_database.h"
#include "charger_factory.h"
#include "constants.h"

// Usage: ev_charger [database-file]
// Charger models are read from a SQLite database (default: chargers.db in the
// current folder). A new database is filled with the default chargers; after
// that you can edit it with any SQLite tool, e.g.
//   sqlite3 chargers.db "UPDATE chargers SET price_per_kwh = 0.55 WHERE name = 'ABB'"
int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : constants::kDefaultDatabasePath;

    try {
        ChargerDatabase db(path);
        if (db.count() == 0) db.seedDefaults();
        std::cout << "Charger database: " << path << " (" << db.count() << " chargers)\n";

        for (const ChargerSpec& spec : db.all()) {
            const auto charger = makeCharger(spec);
            Battery battery(constants::demo::kBatteryCapacityKwh,
                            constants::demo::kBatteryStartPercent);
            charger->plugIn(battery);
            charger->startCharging();
            charger->charge(constants::demo::kChargeSeconds, constants::demo::kCarRequestKw);
            charger->stopCharging();

            std::cout << spec.name << " (" << spec.max_power_kw << " kW, $" << spec.price_per_kwh
                      << "/kWh): " << charger->energyDeliveredKwh() << " kWh delivered, $"
                      << charger->sessionCost() << ", battery now "
                      << battery.stateOfChargePercent() << "%\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
