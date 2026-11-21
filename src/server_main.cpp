#include <httplib.h>

#include <iostream>
#include <string>

#include "charger_database.h"
#include "constants.h"
#include "rest_api.h"

// Usage: ev_charger_server [database-file] [port] [bind-address]
// Serves the charger library over HTTP (see include/rest_api.h for the routes).
// Listens on 127.0.0.1 by default. There is no authentication, so only pass
// 0.0.0.0 (as the Docker image does) on a network you trust.
int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : constants::kDefaultDatabasePath;
    const int port = argc > 2 ? std::stoi(argv[2]) : 8080;
    const std::string host = argc > 3 ? argv[3] : "127.0.0.1";

    try {
        ChargerDatabase db(path);
        if (db.count() == 0) db.seedDefaults();

        RestApi api(db);
        httplib::Server server;
        api.registerRoutes(server);

        std::cout << "EV charger API on http://127.0.0.1:" << port << " (database " << path
                  << ", " << db.count() << " chargers)\n" << std::flush;
        if (!server.listen(host, port)) {
            std::cerr << "error: could not listen on port " << port << '\n';
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
