#include "rest_api.h"

#include <httplib.h>
#include <sys/socket.h>

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "charger_factory.h"
#include "web_ui.h"

using nlohmann::json;

namespace {

constexpr double kDefaultCardBalance = 100.0;
constexpr double kDefaultBatteryKwh = 60.0;
constexpr double kDefaultStartPercent = 20.0;
constexpr double kDefaultRequestedKw = 150.0;

const char* eventName(AppEvent e) {
    switch (e) {
        case AppEvent::ChargingStarted: return "ChargingStarted";
        case AppEvent::StartDeclined: return "StartDeclined";
        case AppEvent::BudgetReached: return "BudgetReached";
        case AppEvent::BatteryFull: return "BatteryFull";
        case AppEvent::ChargingStopped: return "ChargingStopped";
    }
    return "Unknown";
}

const char* stateName(ChargerState s) {
    switch (s) {
        case ChargerState::Available: return "available";
        case ChargerState::Connected: return "connected";
        case ChargerState::Charging: return "charging";
        case ChargerState::Finished: return "finished";
        case ChargerState::Faulted: return "faulted";
    }
    return "unknown";
}

json specJson(const ChargerSpec& s) {
    return {{"name", s.name},
            {"type", s.type},
            {"max_power_kw", s.max_power_kw},
            {"price_per_kwh", s.price_per_kwh},
            {"session_fee", s.session_fee},
            {"taper_above_percent", s.taper_above_percent},
            {"taper_factor", s.taper_factor}};
}

json receiptJson(const Receipt& r) {
    return {{"energy_before_kwh", r.energy_before_kwh},
            {"energy_after_kwh", r.energy_after_kwh},
            {"energy_delivered_kwh", r.energy_delivered_kwh},
            {"amount_charged", r.amount_charged},
            {"amount_released", r.amount_released},
            {"paid", r.paid},
            {"energy_matches", r.energy_matches}};
}

void reply(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void fail(httplib::Response& res, int status, const std::string& message) {
    reply(res, status, {{"error", message}});
}

// A parsed request body. Missing body counts as {}. Any problem sets `error`.
struct Body {
    json data = json::object();
    std::string error;

    explicit Body(const httplib::Request& req) {
        if (req.body.empty()) return;
        data = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
        if (!data.is_object()) error = "body must be a JSON object";
    }

    // Reads a number. If absent: the default, or an error when `required`.
    double number(const char* key, double fallback, bool required = false) {
        if (!error.empty()) return fallback;
        auto it = data.find(key);
        if (it == data.end()) {
            if (required) error = std::string("missing field: ") + key;
            return fallback;
        }
        if (!it->is_number()) {
            error = std::string("field must be a number: ") + key;
            return fallback;
        }
        return it->get<double>();
    }
};

}  // namespace

struct RestApiHandlers {
    static json statusJson(const RestApi::Session& s) {
        const AppStatus st = s.app.status();
        json events = json::array();
        for (AppEvent e : s.app.events()) events.push_back(eventName(e));
        return {{"charger_state", stateName(s.charger->state())},
                {"connected", st.connected},
                {"session_active", st.session_active},
                {"charging", st.charging},
                {"battery_percent", st.battery_percent},
                {"energy_delivered_kwh", st.energy_delivered_kwh},
                {"cost_so_far", st.cost_so_far},
                {"events", events}};
    }

    static RestApi::Session* find(RestApi& api, const std::string& name) {
        auto it = api.sessions_.find(name);
        return it == api.sessions_.end() ? nullptr : it->second.get();
    }

    static void list(RestApi& api, const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        json out = json::array();
        for (const ChargerSpec& spec : api.repository_.all()) {
            json j = specJson(spec);
            j["in_use"] = api.sessions_.count(spec.name) > 0;
            out.push_back(j);
        }
        reply(res, 200, out);
    }

    static void get(RestApi& api, const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        const std::string name = req.path_params.at("name");
        auto spec = api.repository_.find(name);
        if (!spec) return fail(res, 404, "no such charger: " + name);
        json j = specJson(*spec);
        j["in_use"] = api.sessions_.count(name) > 0;
        reply(res, 200, j);
    }

    static void start(RestApi& api, const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        const std::string name = req.path_params.at("name");
        auto spec = api.repository_.find(name);
        if (!spec) return fail(res, 404, "no such charger: " + name);

        Body body(req);
        const double budget = body.number("budget", 0.0, /*required=*/true);
        const double balance = body.number("card_balance", kDefaultCardBalance);
        const double capacity = body.number("battery_capacity_kwh", kDefaultBatteryKwh);
        const double percent = body.number("battery_percent", kDefaultStartPercent);
        if (!body.error.empty()) return fail(res, 400, body.error);

        if (api.sessions_.count(name)) return fail(res, 409, "charger already has a session");

        try {
            // A fresh charger is built from the stored spec, so price changes apply.
            auto session = std::make_unique<RestApi::Session>(makeCharger(*spec), capacity,
                                                              percent, balance);
            session->app.connect();
            switch (session->app.startCharge(budget)) {
                case StartResult::Started:
                    break;
                case StartResult::PaymentDeclined:
                    return fail(res, 402, "payment declined");
                case StartResult::ChargerUnavailable:
                    return fail(res, 409, "charger unavailable");
                case StartResult::AlreadyCharging:
                case StartResult::NotConnected:
                    return fail(res, 409, "cannot start");
            }
            json out = statusJson(*session);
            api.sessions_[name] = std::move(session);
            reply(res, 201, out);
        } catch (const std::invalid_argument& e) {
            fail(res, 400, e.what());
        }
    }

    static void status(RestApi& api, const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        const std::string name = req.path_params.at("name");
        RestApi::Session* s = find(api, name);
        if (!s) return fail(res, 404, "no session on charger: " + name);
        reply(res, 200, statusJson(*s));
    }

    static void advance(RestApi& api, const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        const std::string name = req.path_params.at("name");
        RestApi::Session* s = find(api, name);
        if (!s) return fail(res, 404, "no session on charger: " + name);

        Body body(req);
        const double seconds = body.number("seconds", 0.0, /*required=*/true);
        const double kw = body.number("requested_kw", kDefaultRequestedKw);
        if (!body.error.empty()) return fail(res, 400, body.error);

        try {
            s->app.advance(seconds, kw);
            reply(res, 200, statusJson(*s));
        } catch (const std::invalid_argument& e) {
            fail(res, 400, e.what());
        }
    }

    static void stop(RestApi& api, const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(api.mutex_);
        const std::string name = req.path_params.at("name");
        RestApi::Session* s = find(api, name);
        if (!s) return fail(res, 404, "no session on charger: " + name);

        StopResult result = s->app.stopCharge();
        if (result.status != StopStatus::Stopped) return fail(res, 409, "could not stop");
        json out = {{"receipt", receiptJson(result.receipt)}, {"events", statusJson(*s)["events"]}};
        api.sessions_.erase(name);
        reply(res, 200, out);
    }
};

RestApi::RestApi(ChargerRepository& repository) : repository_(repository) {}
RestApi::~RestApi() = default;

void RestApi::registerRoutes(httplib::Server& server) {
    // cpp-httplib enables SO_REUSEPORT by default, which lets a second copy of
    // the server bind the same port. Sessions live in each process's memory, so
    // requests would then be split between the copies and sessions would seem to
    // vanish. Allow only SO_REUSEADDR, so a second copy fails to start instead.
    server.set_socket_options([](socket_t sock) {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    });
    using H = RestApiHandlers;
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        // The page uses inline script and style and talks only to this server.
        res.set_header("Content-Security-Policy",
                       "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                       "connect-src 'self'");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_content(webUiHtml(), "text/html; charset=utf-8");
    });
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        reply(res, 200, {{"status", "ok"}});
    });
    server.Get("/chargers", [this](auto& rq, auto& rs) { H::list(*this, rq, rs); });
    server.Get("/chargers/:name", [this](auto& rq, auto& rs) { H::get(*this, rq, rs); });
    server.Post("/chargers/:name/session", [this](auto& rq, auto& rs) { H::start(*this, rq, rs); });
    server.Get("/chargers/:name/session", [this](auto& rq, auto& rs) { H::status(*this, rq, rs); });
    server.Post("/chargers/:name/session/advance",
                [this](auto& rq, auto& rs) { H::advance(*this, rq, rs); });
    server.Delete("/chargers/:name/session", [this](auto& rq, auto& rs) { H::stop(*this, rq, rs); });
    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) fail(res, res.status, "not found or not allowed");
    });
}
