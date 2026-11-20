// MASTER TEST 2: a charge triggered by the phone app.
//
// The driver presses Start/Stop in the app -> the app talks to the charger and
// the payment method on file. Each scenario is an end-to-end run through
// ChargingApp, PaymentSession, PaymentTerminal, Charger and Battery,
// repeated for every charger type.
//
// Run just this suite:  charger_tests --gtest_filter='*AppMaster*'

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "battery.h"
#include "charger.h"
#include "charging_app.h"
#include "master_test_support.h"
#include "payment_session.h"
#include "payment_terminal.h"

using Events = std::vector<AppEvent>;

class AppMasterTest : public ::testing::TestWithParam<ChargerKind> {
protected:
    static constexpr double kCardBalance = 100.0;

    PaymentTerminal card{kCardBalance};  // the card on file in the app
    std::unique_ptr<Charger> charger = GetParam().make();
    Battery battery{100.0, 20.0};  // 100 kWh at 20% = 20 kWh
    ChargingApp app{*charger, battery, card};

    void SetUp() override { app.connect(); }

    double expectedCost(double kwh) const {
        return charger->sessionFee() + kwh * charger->pricePerKwh();
    }
};

TEST_P(AppMasterTest, FullHappyPath) {
    const double hold = 50;
    ASSERT_EQ(app.startCharge(hold), StartResult::Started);
    EXPECT_TRUE(app.status().charging);

    const double step_kwh = charger->deliveredPowerKw(20) * 300 / 3600.0;
    app.advance(300, 20);
    app.advance(300, 20);

    const AppStatus mid = app.status();
    EXPECT_NEAR(mid.energy_delivered_kwh, 2 * step_kwh, 1e-9);
    EXPECT_NEAR(mid.cost_so_far, expectedCost(2 * step_kwh), 1e-9);

    const StopResult stop = app.stopCharge();
    ASSERT_EQ(stop.status, StopStatus::Stopped);
    expectReceiptConsistent(stop.receipt, hold);
    EXPECT_TRUE(stop.receipt.paid);
    EXPECT_NEAR(stop.receipt.energy_before_kwh, 20.0, 1e-9);
    EXPECT_NEAR(stop.receipt.amount_charged, expectedCost(2 * step_kwh), 1e-9);
    EXPECT_NEAR(card.cardBalance(), kCardBalance - stop.receipt.amount_charged, 1e-9);
    EXPECT_EQ(charger->state(), ChargerState::Available);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::ChargingStopped}), app.events());
    ASSERT_TRUE(app.lastReceipt().has_value());
    EXPECT_DOUBLE_EQ(app.lastReceipt()->amount_charged, stop.receipt.amount_charged);
}

TEST_P(AppMasterTest, BatteryPercentRisesWithEveryStep) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    double last = app.status().battery_percent;
    EXPECT_DOUBLE_EQ(last, 20.0);
    for (int i = 0; i < 4; ++i) {
        app.advance(120, 20);
        const double now = app.status().battery_percent;
        EXPECT_GT(now, last);
        last = now;
    }
}

TEST_P(AppMasterTest, StatusBeforeStopMatchesTheReceipt) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(600, 20);
    const AppStatus before_stop = app.status();
    const StopResult stop = app.stopCharge();

    EXPECT_NEAR(before_stop.energy_delivered_kwh, stop.receipt.energy_delivered_kwh, 1e-9);
    EXPECT_NEAR(before_stop.cost_so_far, stop.receipt.amount_charged, 1e-9);
}

TEST_P(AppMasterTest, StartWhileDisconnectedDoesNothing) {
    app.disconnect();
    EXPECT_EQ(app.startCharge(50), StartResult::NotConnected);
    EXPECT_EQ(charger->state(), ChargerState::Available);
    EXPECT_FALSE(card.isAuthorized());
    EXPECT_TRUE(app.events().empty());
}

TEST_P(AppMasterTest, DeclinedCardIsReportedAndNothingStarts) {
    PaymentTerminal poor_card(2.0);
    ChargingApp poor_app(*charger, battery, poor_card);
    poor_app.connect();

    EXPECT_EQ(poor_app.startCharge(50), StartResult::PaymentDeclined);
    EXPECT_EQ(charger->state(), ChargerState::Available);
    EXPECT_EQ(poor_app.events(), Events{AppEvent::StartDeclined});
}

TEST_P(AppMasterTest, FaultedChargerCanBeUsedAgainOnceCleared) {
    charger->reportFault();
    EXPECT_EQ(app.startCharge(50), StartResult::ChargerUnavailable);
    EXPECT_FALSE(card.isAuthorized());

    charger->clearFault();
    EXPECT_EQ(app.startCharge(50), StartResult::Started);
}

TEST_P(AppMasterTest, ChargerOccupiedByAnotherCarBlocksTheStart) {
    charger->plugIn();  // someone else is plugged in
    EXPECT_EQ(app.startCharge(50), StartResult::ChargerUnavailable);

    charger->unplug();
    EXPECT_EQ(app.startCharge(50), StartResult::Started);
}

TEST_P(AppMasterTest, ChargerBusyWithACardTapSessionBlocksTheApp) {
    PaymentTerminal other_card(100.0);
    PaymentSession tap_session(other_card, *charger, battery);
    ASSERT_TRUE(tap_session.start(30));  // another driver tapped a card first

    EXPECT_EQ(app.startCharge(50), StartResult::ChargerUnavailable);
    EXPECT_FALSE(card.isAuthorized());  // the app's card was not touched

    tap_session.finish();
    EXPECT_EQ(app.startCharge(50), StartResult::Started);
}

TEST_P(AppMasterTest, SecondStartWhileChargingIsRejected) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    EXPECT_EQ(app.startCharge(50), StartResult::AlreadyCharging);
}

TEST_P(AppMasterTest, StopWhileOfflineFailsAndChargingContinuesUntilReconnect) {
    const double hold = 50;
    ASSERT_EQ(app.startCharge(hold), StartResult::Started);
    app.advance(300, 20);
    const double energy_at_drop = battery.energyKwh();

    app.disconnect();
    EXPECT_EQ(app.stopCharge().status, StopStatus::NotConnected);
    EXPECT_EQ(charger->state(), ChargerState::Charging);
    app.advance(300, 20);  // the charger carries on without the phone
    EXPECT_GT(battery.energyKwh(), energy_at_drop);

    app.connect();
    const StopResult stop = app.stopCharge();
    ASSERT_EQ(stop.status, StopStatus::Stopped);
    expectReceiptConsistent(stop.receipt, hold);
    EXPECT_TRUE(stop.receipt.paid);
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(AppMasterTest, SmallBudgetStopsTheChargeAndNotifiesOnce) {
    const double hold = 5;
    ASSERT_EQ(app.startCharge(hold), StartResult::Started);
    app.advance(3600 * 2, 1000);
    app.advance(3600, 1000);  // nothing more should happen

    EXPECT_FALSE(app.status().charging);
    EXPECT_EQ(charger->state(), ChargerState::Finished);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::BudgetReached}), app.events());

    const StopResult stop = app.stopCharge();
    ASSERT_EQ(stop.status, StopStatus::Stopped);
    expectReceiptConsistent(stop.receipt, hold);
    EXPECT_NEAR(stop.receipt.amount_charged, hold, 1e-9);
}

TEST_P(AppMasterTest, FullBatteryStopsTheChargeAndNotifies) {
    const double hold = 100;
    ASSERT_EQ(app.startCharge(hold), StartResult::Started);
    app.advance(3600 * 10, 1000);  // only 80 kWh of room

    EXPECT_TRUE(battery.isFull());
    EXPECT_FALSE(app.status().charging);
    EXPECT_DOUBLE_EQ(app.status().battery_percent, 100.0);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::BatteryFull}), app.events());

    const StopResult stop = app.stopCharge();
    expectReceiptConsistent(stop.receipt, hold);
    EXPECT_NEAR(stop.receipt.energy_delivered_kwh, 80.0, 1e-9);
    EXPECT_NEAR(stop.receipt.amount_charged, expectedCost(80.0), 1e-9);
}

TEST_P(AppMasterTest, ChargerFaultMidSessionStillLetsTheDriverStopAndPay) {
    const double hold = 50;
    ASSERT_EQ(app.startCharge(hold), StartResult::Started);
    app.advance(300, 20);
    charger->reportFault();
    EXPECT_FALSE(app.status().charging);

    const StopResult stop = app.stopCharge();
    ASSERT_EQ(stop.status, StopStatus::Stopped);
    expectReceiptConsistent(stop.receipt, hold);
    EXPECT_NEAR(stop.receipt.amount_charged, expectedCost(stop.receipt.energy_delivered_kwh), 1e-9);
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(AppMasterTest, StartThenImmediateStopMovesNoEnergy) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    const StopResult stop = app.stopCharge();

    expectReceiptConsistent(stop.receipt, 50);
    EXPECT_DOUBLE_EQ(stop.receipt.energy_before_kwh, stop.receipt.energy_after_kwh);
    EXPECT_NEAR(stop.receipt.amount_charged, charger->sessionFee(), 1e-9);
}

TEST_P(AppMasterTest, SecondStopReportsNoActiveSession) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    ASSERT_EQ(app.stopCharge().status, StopStatus::Stopped);
    EXPECT_EQ(app.stopCharge().status, StopStatus::NoActiveSession);
}

TEST_P(AppMasterTest, BackToBackSessionsPickUpWhereTheLastEnded) {
    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(600, 20);
    const StopResult first = app.stopCharge();

    ASSERT_EQ(app.startCharge(50), StartResult::Started);
    app.advance(600, 20);
    const StopResult second = app.stopCharge();

    EXPECT_NEAR(second.receipt.energy_before_kwh, first.receipt.energy_after_kwh, 1e-9);
    EXPECT_GT(second.receipt.energy_after_kwh, first.receipt.energy_after_kwh);
    EXPECT_EQ((Events{AppEvent::ChargingStarted, AppEvent::ChargingStopped,
                      AppEvent::ChargingStarted, AppEvent::ChargingStopped}),
              app.events());
}

TEST_P(AppMasterTest, CardBalanceIsConservedAcrossSeveralSessions) {
    double total_charged = 0;
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(app.startCharge(30), StartResult::Started);
        app.advance(600, 20);
        const StopResult stop = app.stopCharge();
        expectReceiptConsistent(stop.receipt, 30);
        total_charged += stop.receipt.amount_charged;
    }
    EXPECT_NEAR(card.cardBalance(), kCardBalance - total_charged, 1e-9);
    EXPECT_FALSE(card.isAuthorized());
}

INSTANTIATE_TEST_SUITE_P(AllChargers, AppMasterTest,
                         ::testing::ValuesIn(allChargerKinds()), chargerKindName);
