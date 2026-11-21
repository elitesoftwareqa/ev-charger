#include <httplib.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <thread>

#include "charger_repository.h"
#include "rest_api.h"

using nlohmann::json;

// Runs a real HTTP server on a free port with an in-memory charger list, and
// talks to it with a real HTTP client.
class RestApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        repo_.seedDefaults();
        api_ = std::make_unique<RestApi>(repo_);
        api_->registerRoutes(server_);
        port_ = server_.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port_, 0);
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
        client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    }
    void TearDown() override {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    json body(const httplib::Result& r) { return json::parse(r->body); }
    static std::string session(const std::string& name = "ABB") {
        return "/chargers/" + httplib::detail::encode_url(name) + "/session";
    }
    httplib::Result post(const std::string& path, const json& j) {
        return client_->Post(path, j.dump(), "application/json");
    }

    InMemoryChargerRepository repo_;
    std::unique_ptr<RestApi> api_;
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
    std::unique_ptr<httplib::Client> client_;
};

TEST_F(RestApiTest, HealthIsOk) {
    auto r = client_->Get("/health");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(body(r)["status"], "ok");
}

TEST_F(RestApiTest, ListsAllChargers) {
    auto r = client_->Get("/chargers");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(body(r).size(), 3u);
    EXPECT_EQ(body(r)[0]["in_use"], false);
}

TEST_F(RestApiTest, GetsOneChargerIncludingNameWithSpaces) {
    auto r = client_->Get("/chargers/" + httplib::detail::encode_url("Generic 11 kW"));
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 200);
    EXPECT_EQ(body(r)["name"], "Generic 11 kW");
}

TEST_F(RestApiTest, UnknownChargerIs404) {
    auto r = client_->Get("/chargers/Nope");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404);
    EXPECT_TRUE(body(r).contains("error"));
}

TEST_F(RestApiTest, UnknownRouteIsJson404) {
    auto r = client_->Get("/nothing");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 404);
    EXPECT_TRUE(body(r).contains("error"));
}

TEST_F(RestApiTest, StartRequiresBudget) {
    auto r = post(session(), json::object());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 400);
}

TEST_F(RestApiTest, StartRejectsNonNumberAndBadJson) {
    EXPECT_EQ(post(session(), {{"budget", "ten"}})->status, 400);
    EXPECT_EQ(client_->Post(session(), "not json", "application/json")->status, 400);
}

TEST_F(RestApiTest, StartRejectsNonPositiveBudget) {
    EXPECT_EQ(post(session(), {{"budget", 0}})->status, 400);
    EXPECT_EQ(post(session(), {{"budget", -5}})->status, 400);
}

TEST_F(RestApiTest, StartOnUnknownChargerIs404) {
    EXPECT_EQ(post(session("Nope"), {{"budget", 10}})->status, 404);
}

TEST_F(RestApiTest, DeclinedCardIs402AndLeavesNoSession) {
    auto r = post(session(), {{"budget", 50}, {"card_balance", 10}});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, 402);
    EXPECT_EQ(client_->Get(session())->status, 404);
}

TEST_F(RestApiTest, SecondSessionOnSameChargerIs409) {
    ASSERT_EQ(post(session(), {{"budget", 10}})->status, 201);
    EXPECT_EQ(post(session(), {{"budget", 10}})->status, 409);
}

TEST_F(RestApiTest, StartedChargerIsMarkedInUse) {
    ASSERT_EQ(post(session(), {{"budget", 10}})->status, 201);
    auto r = client_->Get("/chargers/ABB");
    EXPECT_EQ(body(r)["in_use"], true);
}

TEST_F(RestApiTest, FullSessionStartAdvanceStop) {
    auto start = post(session(), {{"budget", 20}});
    ASSERT_TRUE(start);
    ASSERT_EQ(start->status, 201);
    EXPECT_EQ(body(start)["charging"], true);
    EXPECT_EQ(body(start)["events"][0], "ChargingStarted");

    auto adv = post(session() + "/advance", {{"seconds", 60}, {"requested_kw", 100}});
    ASSERT_TRUE(adv);
    ASSERT_EQ(adv->status, 200);
    EXPECT_GT(body(adv)["energy_delivered_kwh"].get<double>(), 0.0);
    EXPECT_GT(body(adv)["battery_percent"].get<double>(), 20.0);

    auto status = client_->Get(session());
    ASSERT_EQ(status->status, 200);
    EXPECT_EQ(body(status)["session_active"], true);

    auto stop = client_->Delete(session());
    ASSERT_TRUE(stop);
    ASSERT_EQ(stop->status, 200);
    const json receipt = body(stop)["receipt"];
    EXPECT_EQ(receipt["paid"], true);
    EXPECT_EQ(receipt["energy_matches"], true);
    EXPECT_LE(receipt["amount_charged"].get<double>(), 20.0);
    EXPECT_NEAR(receipt["amount_charged"].get<double>() + receipt["amount_released"].get<double>(),
                20.0, 1e-9);

    // The charger is free again.
    EXPECT_EQ(client_->Get(session())->status, 404);
    EXPECT_EQ(post(session(), {{"budget", 10}})->status, 201);
}

TEST_F(RestApiTest, BudgetRunningOutIsReportedAsEvent) {
    ASSERT_EQ(post(session(), {{"budget", 1}})->status, 201);
    auto adv = post(session() + "/advance", {{"seconds", 600}, {"requested_kw", 150}});
    ASSERT_EQ(adv->status, 200);
    bool seen = false;
    const json events = body(adv)["events"];
    for (const auto& e : events) seen = seen || e == "BudgetReached";
    EXPECT_TRUE(seen);
    auto stop = client_->Delete(session());
    EXPECT_NEAR(body(stop)["receipt"]["amount_charged"].get<double>(), 1.0, 1e-9);
}

TEST_F(RestApiTest, BatteryFullIsReportedAsEvent) {
    ASSERT_EQ(post(session(), {{"budget", 100}, {"battery_percent", 99}, {"battery_capacity_kwh", 10}})->status, 201);
    auto adv = post(session() + "/advance", {{"seconds", 600}});
    ASSERT_EQ(adv->status, 200);
    EXPECT_EQ(body(adv)["battery_percent"], 100.0);
    bool seen = false;
    const json events = body(adv)["events"];
    for (const auto& e : events) seen = seen || e == "BatteryFull";
    EXPECT_TRUE(seen);
}

TEST_F(RestApiTest, AdvanceValidatesInput) {
    ASSERT_EQ(post(session(), {{"budget", 10}})->status, 201);
    EXPECT_EQ(post(session() + "/advance", json::object())->status, 400);
    EXPECT_EQ(post(session() + "/advance", {{"seconds", "x"}})->status, 400);
}

TEST_F(RestApiTest, AdvanceStopStatusWithoutSessionAre404) {
    EXPECT_EQ(post(session() + "/advance", {{"seconds", 1}})->status, 404);
    EXPECT_EQ(client_->Delete(session())->status, 404);
    EXPECT_EQ(client_->Get(session())->status, 404);
}

TEST_F(RestApiTest, DifferentChargersCanRunAtOnce) {
    ASSERT_EQ(post(session("ABB"), {{"budget", 10}})->status, 201);
    ASSERT_EQ(post(session("BTC"), {{"budget", 10}})->status, 201);
    EXPECT_EQ(client_->Delete(session("ABB"))->status, 200);
    EXPECT_EQ(client_->Get(session("BTC"))->status, 200);
}

TEST_F(RestApiTest, BtcSessionFeeShowsOnReceipt) {
    ASSERT_EQ(post(session("BTC"), {{"budget", 20}})->status, 201);
    ASSERT_EQ(post(session("BTC") + "/advance", {{"seconds", 60}, {"requested_kw", 60}})->status, 200);
    auto stop = client_->Delete(session("BTC"));
    // 1 minute at 60 kW = 1 kWh at $0.40 plus the $1.00 fee.
    EXPECT_NEAR(body(stop)["receipt"]["amount_charged"].get<double>(), 1.40, 1e-6);
}

TEST_F(RestApiTest, SecondServerCannotShareThePort) {
    // A second copy must fail to start, not silently share the port and split
    // requests (and therefore sessions) between two processes.
    InMemoryChargerRepository other_repo;
    RestApi other_api(other_repo);
    httplib::Server other;
    other_api.registerRoutes(other);
    EXPECT_FALSE(other.bind_to_port("127.0.0.1", port_));
}
