// MASTER TEST 1: a charge triggered by the payment terminal.
//
// The driver taps a card -> the terminal places a hold -> the charger starts.
// Each scenario is an end-to-end run through PaymentTerminal, PaymentSession,
// Charger and Battery, repeated for every charger type.
//
// Run just this suite:  charger_tests --gtest_filter='*PayterMaster*'

#include <gtest/gtest.h>

#include <memory>

#include "battery.h"
#include "charger.h"
#include "master_test_support.h"
#include "payment_session.h"
#include "payment_terminal.h"

class PayterMasterTest : public ::testing::TestWithParam<ChargerKind> {
protected:
    static constexpr double kCardBalance = 100.0;

    PaymentTerminal terminal{kCardBalance};
    std::unique_ptr<Charger> charger = GetParam().make();
    Battery battery{100.0, 20.0};  // 100 kWh at 20% = 20 kWh
    PaymentSession session{terminal, *charger, battery};

    // --- the driver's actions ---
    bool tapCard(double hold) { return session.start(hold); }
    void driveFor(double seconds, double car_kw) { session.charge(seconds, car_kw); }
    Receipt removeCar() { return session.finish(); }

    // What a session of `kwh` should cost on this charger.
    double expectedCost(double kwh) const {
        return charger->sessionFee() + kwh * charger->pricePerKwh();
    }
};

TEST_P(PayterMasterTest, FullHappyPath) {
    const double hold = 50;
    ASSERT_TRUE(tapCard(hold));
    EXPECT_EQ(charger->state(), ChargerState::Charging);
    EXPECT_DOUBLE_EQ(terminal.authorizedAmount(), hold);

    const double expected_kwh = charger->deliveredPowerKw(20) * 600 / 3600.0;
    driveFor(600, 20);
    const Receipt r = removeCar();

    expectReceiptConsistent(r, hold);
    EXPECT_TRUE(r.paid);
    EXPECT_NEAR(r.energy_before_kwh, 20.0, 1e-9);
    EXPECT_NEAR(r.energy_delivered_kwh, expected_kwh, 1e-9);
    EXPECT_NEAR(r.amount_charged, expectedCost(expected_kwh), 1e-9);
    EXPECT_NEAR(terminal.cardBalance(), kCardBalance - r.amount_charged, 1e-9);
    EXPECT_FALSE(terminal.isAuthorized());
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(PayterMasterTest, BatteryReadingMatchesReceiptAfterTheSession) {
    ASSERT_TRUE(tapCard(50));
    driveFor(600, 20);
    const Receipt r = removeCar();
    EXPECT_DOUBLE_EQ(battery.energyKwh(), r.energy_after_kwh);
}

TEST_P(PayterMasterTest, DeclinedCardNeverStartsTheCharger) {
    PaymentTerminal poor_card(2.0);
    PaymentSession poor_session(poor_card, *charger, battery);

    EXPECT_FALSE(poor_session.start(10));
    EXPECT_EQ(charger->state(), ChargerState::Available);
    EXPECT_FALSE(poor_card.isAuthorized());
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 20.0);
}

TEST_P(PayterMasterTest, OfflineTerminalAtTapNeverStartsTheCharger) {
    terminal.setOffline(true);
    EXPECT_FALSE(tapCard(50));
    EXPECT_EQ(charger->state(), ChargerState::Available);
    EXPECT_FALSE(session.active());
}

TEST_P(PayterMasterTest, FaultedChargerRefusesAndLeavesNoHold) {
    charger->reportFault();
    EXPECT_FALSE(tapCard(50));
    EXPECT_FALSE(terminal.isAuthorized());
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), kCardBalance);
}

TEST_P(PayterMasterTest, SecondTapDuringASessionIsRejected) {
    ASSERT_TRUE(tapCard(50));
    EXPECT_FALSE(tapCard(50));
    EXPECT_DOUBLE_EQ(terminal.authorizedAmount(), 50);
}

TEST_P(PayterMasterTest, TerminalDroppingMidSessionIsReportedAsUnpaid) {
    const double hold = 50;
    ASSERT_TRUE(tapCard(hold));
    driveFor(600, 20);
    terminal.setOffline(true);  // connection lost before the charge is captured
    const Receipt r = removeCar();

    expectReceiptConsistent(r, hold);
    EXPECT_FALSE(r.paid);
    EXPECT_GT(r.energy_delivered_kwh, 0.0);  // energy was still given
    EXPECT_DOUBLE_EQ(r.amount_charged, 0.0);
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), kCardBalance);
    EXPECT_FALSE(terminal.isAuthorized());  // no dangling hold
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(PayterMasterTest, SmallHoldCapsTheChargeAndTheBill) {
    const double hold = 5;
    ASSERT_TRUE(tapCard(hold));
    driveFor(3600 * 2, 1000);  // wants far more than $5 buys
    EXPECT_TRUE(session.budgetExhausted());
    EXPECT_EQ(charger->state(), ChargerState::Finished);

    const Receipt r = removeCar();
    expectReceiptConsistent(r, hold);
    EXPECT_NEAR(r.amount_charged, hold, 1e-9);
    EXPECT_NEAR(r.amount_released, 0.0, 1e-9);
    EXPECT_NEAR(terminal.cardBalance(), kCardBalance - hold, 1e-9);
}

TEST_P(PayterMasterTest, FullBatteryEndsTheChargeAndReleasesTheRestOfTheHold) {
    const double hold = 100;
    ASSERT_TRUE(tapCard(hold));
    driveFor(3600 * 10, 1000);  // only 80 kWh of room
    EXPECT_TRUE(battery.isFull());
    EXPECT_FALSE(session.budgetExhausted());  // the car filled up before the money ran out

    const Receipt r = removeCar();
    expectReceiptConsistent(r, hold);
    EXPECT_NEAR(r.energy_after_kwh, 100.0, 1e-9);
    EXPECT_NEAR(r.energy_delivered_kwh, 80.0, 1e-9);
    EXPECT_NEAR(r.amount_charged, expectedCost(80.0), 1e-9);
    EXPECT_GT(r.amount_released, 0.0);
}

TEST_P(PayterMasterTest, ChargerFaultMidSessionStillSettlesWhatWasDelivered) {
    const double hold = 50;
    ASSERT_TRUE(tapCard(hold));
    driveFor(600, 20);
    charger->reportFault();
    driveFor(600, 20);  // no further energy while faulted
    const Receipt r = removeCar();

    expectReceiptConsistent(r, hold);
    EXPECT_TRUE(r.paid);
    EXPECT_NEAR(r.amount_charged, expectedCost(r.energy_delivered_kwh), 1e-9);
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(PayterMasterTest, TapAndRemoveWithoutChargingMovesNoEnergy) {
    ASSERT_TRUE(tapCard(50));
    const Receipt r = removeCar();

    expectReceiptConsistent(r, 50);
    EXPECT_DOUBLE_EQ(r.energy_before_kwh, r.energy_after_kwh);
    EXPECT_DOUBLE_EQ(r.energy_delivered_kwh, 0.0);
    EXPECT_NEAR(r.amount_charged, charger->sessionFee(), 1e-9);  // only a fee, if any
}

TEST_P(PayterMasterTest, SplittingTheChargeIntoStepsGivesTheSameEnergy) {
    // Below any power taper, one long step and two short steps must agree.
    auto charger2 = GetParam().make();
    Battery battery2(100.0, 20.0);
    PaymentTerminal terminal2(kCardBalance);
    PaymentSession session2(terminal2, *charger2, battery2);

    ASSERT_TRUE(tapCard(50));
    ASSERT_TRUE(session2.start(50));
    driveFor(600, 20);
    session2.charge(300, 20);
    session2.charge(300, 20);

    EXPECT_NEAR(battery.energyKwh(), battery2.energyKwh(), 1e-9);
    removeCar();
    session2.finish();
}

TEST_P(PayterMasterTest, BackToBackSessionsPickUpWhereTheLastEnded) {
    ASSERT_TRUE(tapCard(50));
    driveFor(600, 20);
    const Receipt first = removeCar();

    ASSERT_TRUE(tapCard(50));
    driveFor(600, 20);
    const Receipt second = removeCar();

    EXPECT_NEAR(second.energy_before_kwh, first.energy_after_kwh, 1e-9);
    EXPECT_GT(second.energy_after_kwh, first.energy_after_kwh);
}

TEST_P(PayterMasterTest, CardBalanceIsConservedAcrossSeveralSessions) {
    double total_charged = 0;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(tapCard(30));
        driveFor(600, 20);
        const Receipt r = removeCar();
        expectReceiptConsistent(r, 30);
        total_charged += r.amount_charged;
    }
    EXPECT_NEAR(terminal.cardBalance(), kCardBalance - total_charged, 1e-9);
    EXPECT_FALSE(terminal.isAuthorized());
}

INSTANTIATE_TEST_SUITE_P(AllChargers, PayterMasterTest,
                         ::testing::ValuesIn(allChargerKinds()), chargerKindName);
