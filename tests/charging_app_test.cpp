#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abb_charger.h"
#include "battery.h"
#include "btc_charger.h"
#include "charger.h"
#include "charging_app.h"
#include "payment_terminal.h"

using Events = std::vector<AppEvent>;

class ChargingAppTest : public ::testing::Test {
protected:
    PaymentTerminal card{100.0};  // the card on file: $100
    Charger charger{11.0, 0.50};  // 11 kW, $0.50 per kWh
    Battery battery{50.0, 20.0};  // 50 kWh battery at 20% = 10 kWh
    ChargingApp app{charger, battery, card};

    void SetUp() override { app.connect(); }
};

// --- Connection ---

TEST_F(ChargingAppTest, NewAppStartsDisconnectedAndIdle) {
    ChargingApp fresh(charger, battery, card);
    EXPECT_FALSE(fresh.isConnected());
    EXPECT_FALSE(fresh.status().session_active);
    EXPECT_TRUE(fresh.events().empty());
    EXPECT_FALSE(fresh.lastReceipt().has_value());
}

TEST_F(ChargingAppTest, ConnectAndDisconnectToggleStatus) {
    EXPECT_TRUE(app.isConnected());
    app.disconnect();
    EXPECT_FALSE(app.isConnected());
    EXPECT_FALSE(app.status().connected);
}

// --- Starting a charge ---

TEST_F(ChargingAppTest, StartChargeStartsTheCharger) {
    EXPECT_EQ(app.startCharge(20), StartResult::Started);
    EXPECT_EQ(charger.state(), ChargerState::Charging);
    EXPECT_TRUE(app.status().charging);
    EXPECT_TRUE(app.status().session_active);
    EXPECT_EQ(app.events(), Events{AppEvent::ChargingStarted});
}

TEST_F(ChargingAppTest, StartFailsWhenPhoneIsDisconnected) {
    app.disconnect();
    EXPECT_EQ(app.startCharge(20), StartResult::NotConnected);
    EXPECT_EQ(charger.state(), ChargerState::Available);
    EXPECT_FALSE(card.isAuthorized());
}

TEST_F(ChargingAppTest, CannotStartTwice) {
    ASSERT_EQ(app.startCharge(20), StartResult::Started);
    EXPECT_EQ(app.startCharge(20), StartResult::AlreadyCharging);
}

TEST_F(ChargingAppTest, DeclinedCardDoesNotStartAndNotifiesDriver) {
    PaymentTerminal poor_card(1.0);
    ChargingApp poor_app(charger, battery, poor_card);
    poor_app.connect();

    EXPECT_EQ(poor_app.startCharge(20), StartResult::PaymentDeclined);
    EXPECT_EQ(charger.state(), ChargerState::Available);
    EXPECT_EQ(poor_app.events(), Events{AppEvent::StartDeclined});
}

TEST_F(ChargingAppTest, StartFailsWhenChargerIsFaulted) {
    charger.reportFault();
    EXPECT_EQ(app.startCharge(20), StartResult::ChargerUnavailable);
    EXPECT_FALSE(card.isAuthorized());  // no hold placed
}

TEST_F(ChargingAppTest, StartFailsWhenAnotherCarIsPluggedIn) {
    charger.plugIn();  // someone else got there first
    EXPECT_EQ(app.startCharge(20), StartResult::ChargerUnavailable);
}

TEST_F(ChargingAppTest, StartRejectsNonPositiveBudget) {
    EXPECT_THROW(app.startCharge(0), std::invalid_argument);
    EXPECT_THROW(app.startCharge(-5), std::invalid_argument);
}

// --- While charging ---

TEST_F(ChargingAppTest, StatusShowsLiveValues) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    EXPECT_DOUBLE_EQ(app.status().battery_percent, 20.0);

    app.advance(3600, 10);  // 10 kW for 1 hour = 10 kWh
    const AppStatus s = app.status();
    EXPECT_NEAR(s.battery_percent, 40.0, 1e-9);
    EXPECT_NEAR(s.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_NEAR(s.cost_so_far, 5.0, 1e-9);
    EXPECT_TRUE(s.charging);
}

TEST_F(ChargingAppTest, AdvanceWithoutSessionDoesNothing) {
    app.advance(3600, 10);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 10.0);
}

// --- Stopping a charge ---

TEST_F(ChargingAppTest, StopChargeSettlesPaymentAndFreesCharger) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);

    const StopResult r = app.stopCharge();
    EXPECT_EQ(r.status, StopStatus::Stopped);
    EXPECT_TRUE(r.receipt.paid);
    EXPECT_NEAR(r.receipt.amount_charged, 5.0, 1e-9);
    EXPECT_NEAR(card.cardBalance(), 95.0, 1e-9);
    EXPECT_EQ(charger.state(), ChargerState::Available);
    EXPECT_FALSE(app.status().session_active);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::ChargingStopped}), app.events());
}

TEST_F(ChargingAppTest, ReceiptShowsBatteryBeforeAndAfter) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);
    const StopResult r = app.stopCharge();

    EXPECT_NEAR(r.receipt.energy_before_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.receipt.energy_after_kwh, 20.0, 1e-9);
    EXPECT_TRUE(r.receipt.energy_matches);
}

TEST_F(ChargingAppTest, LastReceiptIsRemembered) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);
    app.stopCharge();
    ASSERT_TRUE(app.lastReceipt().has_value());
    EXPECT_NEAR(app.lastReceipt()->amount_charged, 5.0, 1e-9);
}

TEST_F(ChargingAppTest, StopWithNoSessionReportsNoActiveSession) {
    EXPECT_EQ(app.stopCharge().status, StopStatus::NoActiveSession);
}

TEST_F(ChargingAppTest, StatusResetsAfterStop) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);
    app.stopCharge();
    EXPECT_FALSE(app.status().charging);
    EXPECT_DOUBLE_EQ(app.status().energy_delivered_kwh, 0.0);
}

TEST_F(ChargingAppTest, CanChargeAgainAfterStopping) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);
    app.stopCharge();

    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(3600, 10);
    const StopResult r = app.stopCharge();
    EXPECT_NEAR(r.receipt.energy_before_kwh, 20.0, 1e-9);  // picks up where it left off
    EXPECT_NEAR(r.receipt.energy_after_kwh, 30.0, 1e-9);
}

// --- Losing the connection ---

TEST_F(ChargingAppTest, StopFailsWhenPhoneIsDisconnected) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.disconnect();
    EXPECT_EQ(app.stopCharge().status, StopStatus::NotConnected);
    EXPECT_EQ(charger.state(), ChargerState::Charging);  // the stop never arrived
}

TEST_F(ChargingAppTest, ChargingContinuesWhileOfflineThenStopsAfterReconnect) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(1800, 10);  // 5 kWh
    app.disconnect();
    app.advance(1800, 10);  // charger keeps going: another 5 kWh
    EXPECT_NEAR(battery.energyKwh(), 20.0, 1e-9);

    app.connect();
    const StopResult r = app.stopCharge();
    EXPECT_EQ(r.status, StopStatus::Stopped);
    EXPECT_NEAR(r.receipt.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.receipt.amount_charged, 5.0, 1e-9);
}

// --- Charging that ends on its own ---

TEST_F(ChargingAppTest, BudgetReachedStopsChargingAndNotifiesOnce) {
    ASSERT_EQ(app.startCharge(5), StartResult::Started);  // $5 buys 10 kWh
    app.advance(3600 * 5, 10);                            // wants 50 kWh
    app.advance(3600, 10);                                // nothing more should happen

    EXPECT_FALSE(app.status().charging);
    EXPECT_EQ(charger.state(), ChargerState::Finished);
    EXPECT_NEAR(app.status().energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::BudgetReached}), app.events());
}

TEST_F(ChargingAppTest, StopAfterBudgetReachedStillSettles) {
    ASSERT_EQ(app.startCharge(5), StartResult::Started);
    app.advance(3600 * 5, 10);

    const StopResult r = app.stopCharge();
    EXPECT_EQ(r.status, StopStatus::Stopped);
    EXPECT_NEAR(r.receipt.amount_charged, 5.0, 1e-9);
    EXPECT_NEAR(card.cardBalance(), 95.0, 1e-9);
}

TEST_F(ChargingAppTest, FullBatteryStopsChargingAndNotifies) {
    ASSERT_EQ(app.startCharge(100), StartResult::Started);  // plenty of budget
    app.advance(3600 * 10, 10);                             // only 40 kWh of room

    EXPECT_TRUE(battery.isFull());
    EXPECT_FALSE(app.status().charging);
    EXPECT_DOUBLE_EQ(app.status().battery_percent, 100.0);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::BatteryFull}), app.events());
}

TEST_F(ChargingAppTest, FullBatteryOnlyPaysForEnergyDelivered) {
    ASSERT_EQ(app.startCharge(100), StartResult::Started);
    app.advance(3600 * 10, 10);
    const StopResult r = app.stopCharge();

    EXPECT_NEAR(r.receipt.amount_charged, 20.0, 1e-9);   // 40 kWh * $0.50
    EXPECT_NEAR(r.receipt.amount_released, 80.0, 1e-9);
}

// ---------------------------------------------------------------------------
// The same start/stop flow on every charger type
// ---------------------------------------------------------------------------

struct AppChargerType {
    std::string name;
    std::function<std::unique_ptr<Charger>()> make;
};

// Lets gtest show the parameter by name in test listings.
inline void PrintTo(const AppChargerType& k, std::ostream* os) { *os << k.name; }

class ChargingAppAnyChargerTest : public ::testing::TestWithParam<AppChargerType> {
protected:
    PaymentTerminal card{100.0};
    std::unique_ptr<Charger> charger = GetParam().make();
    Battery battery{200.0, 20.0};  // 40 kWh
    ChargingApp app{*charger, battery, card};

    void SetUp() override { app.connect(); }
};

TEST_P(ChargingAppAnyChargerTest, StartThenStopWorks) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(600, 20);
    const StopResult r = app.stopCharge();

    EXPECT_EQ(r.status, StopStatus::Stopped);
    EXPECT_GT(battery.energyKwh(), 40.0);
    EXPECT_TRUE(r.receipt.energy_matches);
    EXPECT_LE(r.receipt.amount_charged, 50.0);
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(ChargingAppAnyChargerTest, BudgetIsRespected) {
    ASSERT_EQ(app.startCharge(5), StartResult::Started);
    app.advance(3600 * 2, 1000);
    const StopResult r = app.stopCharge();
    EXPECT_NEAR(r.receipt.amount_charged, 5.0, 1e-9);
    EXPECT_EQ(app.events().at(1), AppEvent::BudgetReached);
}

TEST_P(ChargingAppAnyChargerTest, DisconnectedStopLeavesChargerRunning) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.disconnect();
    EXPECT_EQ(app.stopCharge().status, StopStatus::NotConnected);
    EXPECT_EQ(charger->state(), ChargerState::Charging);
}

INSTANTIATE_TEST_SUITE_P(
    AllChargers, ChargingAppAnyChargerTest,
    ::testing::Values(
        AppChargerType{"Generic", [] { return std::make_unique<Charger>(11.0, 0.50); }},
        AppChargerType{"ABB", [] { return std::make_unique<AbbCharger>(); }},
        AppChargerType{"BTC", [] { return std::make_unique<BtcCharger>(); }}),
    [](const ::testing::TestParamInfo<AppChargerType>& info) { return info.param.name; });

TEST(ChargingAppBtcTest, StartingAndStoppingImmediatelyStillPaysTheSessionFee) {
    PaymentTerminal card(100.0);
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    ChargingApp app(btc, battery, card);
    app.connect();

    ASSERT_EQ(app.startCharge(20), StartResult::Started);
    const StopResult r = app.stopCharge();
    EXPECT_NEAR(r.receipt.amount_charged, 1.0, 1e-9);
    EXPECT_NEAR(card.cardBalance(), 99.0, 1e-9);
}
