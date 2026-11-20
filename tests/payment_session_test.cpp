#include <gtest/gtest.h>

#include <stdexcept>

#include "battery.h"
#include "charger.h"
#include "payment_session.h"
#include "payment_terminal.h"

class PaymentSessionTest : public ::testing::Test {
protected:
    PaymentTerminal terminal{100.0};  // card with $100
    Charger charger{11.0, 0.50};      // 11 kW, $0.50 per kWh
    Battery battery{50.0, 20.0};      // 50 kWh battery at 20% = 10 kWh
    PaymentSession session{terminal, charger, battery};
};

// --- Starting a session ---

TEST_F(PaymentSessionTest, StartPlacesHoldAndBeginsCharging) {
    ASSERT_TRUE(session.start(20));
    EXPECT_TRUE(session.active());
    EXPECT_DOUBLE_EQ(terminal.authorizedAmount(), 20);
    EXPECT_EQ(charger.state(), ChargerState::Charging);
}

TEST_F(PaymentSessionTest, DeclinedCardDoesNotStartCharging) {
    EXPECT_FALSE(session.start(500));  // more than the card has
    EXPECT_FALSE(session.active());
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(PaymentSessionTest, OfflineTerminalDoesNotStartCharging) {
    terminal.setOffline(true);
    EXPECT_FALSE(session.start(20));
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(PaymentSessionTest, FaultedChargerReleasesTheHold) {
    charger.reportFault();
    EXPECT_FALSE(session.start(20));
    EXPECT_FALSE(terminal.isAuthorized());  // no hold left behind
}

TEST_F(PaymentSessionTest, CannotStartTwice) {
    ASSERT_TRUE(session.start(20));
    EXPECT_FALSE(session.start(20));
}

TEST_F(PaymentSessionTest, StartRejectsNonPositiveAmount) {
    EXPECT_THROW(session.start(0), std::invalid_argument);
}

// --- Energy before and after ---

TEST_F(PaymentSessionTest, ReceiptShowsEnergyBeforeAndAfter) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);  // 10 kW for 1 hour = 10 kWh
    const Receipt r = session.finish();

    EXPECT_NEAR(r.energy_before_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.energy_after_kwh, 20.0, 1e-9);
    EXPECT_NEAR(r.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_TRUE(r.energy_matches);
}

TEST_F(PaymentSessionTest, BatteryMatchesReceiptAfterFinish) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    const Receipt r = session.finish();
    EXPECT_DOUBLE_EQ(battery.energyKwh(), r.energy_after_kwh);
}

TEST_F(PaymentSessionTest, NoChargeMeansBeforeEqualsAfter) {
    ASSERT_TRUE(session.start(50));
    const Receipt r = session.finish();
    EXPECT_DOUBLE_EQ(r.energy_before_kwh, r.energy_after_kwh);
    EXPECT_DOUBLE_EQ(r.energy_delivered_kwh, 0);
    EXPECT_TRUE(r.energy_matches);
}

TEST_F(PaymentSessionTest, EnergyAccumulatesAcrossMultipleCharges) {
    ASSERT_TRUE(session.start(50));
    session.charge(1800, 10);  // 5 kWh
    session.charge(1800, 10);  // 5 kWh
    const Receipt r = session.finish();
    EXPECT_NEAR(r.energy_after_kwh - r.energy_before_kwh, 10.0, 1e-9);
}

// --- Payment settlement ---

TEST_F(PaymentSessionTest, CapturesActualCostAndReleasesTheRest) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);  // 10 kWh * $0.50 = $5
    const Receipt r = session.finish();

    EXPECT_TRUE(r.paid);
    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
    EXPECT_NEAR(r.amount_released, 45.0, 1e-9);
    EXPECT_NEAR(terminal.cardBalance(), 95.0, 1e-9);
    EXPECT_FALSE(terminal.isAuthorized());
}

TEST_F(PaymentSessionTest, ChargerIsAvailableAgainAfterFinish) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    session.finish();
    EXPECT_FALSE(session.active());
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(PaymentSessionTest, FinishWithoutSessionThrows) {
    EXPECT_THROW(session.finish(), std::logic_error);
}

TEST_F(PaymentSessionTest, NoChargeMeansNothingTaken) {
    ASSERT_TRUE(session.start(50));
    const Receipt r = session.finish();
    EXPECT_TRUE(r.paid);
    EXPECT_DOUBLE_EQ(r.amount_charged, 0);
    EXPECT_DOUBLE_EQ(terminal.cardBalance(), 100);
}

// --- Spending limit ---

TEST_F(PaymentSessionTest, StopsWhenPreauthorizedAmountIsUsedUp) {
    ASSERT_TRUE(session.start(5));  // $5 buys 10 kWh at $0.50
    session.charge(3600 * 5, 10);   // car wants 50 kWh
    EXPECT_TRUE(session.budgetExhausted());
    EXPECT_EQ(charger.state(), ChargerState::Finished);

    const Receipt r = session.finish();
    EXPECT_NEAR(r.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.energy_after_kwh - r.energy_before_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
    EXPECT_NEAR(r.amount_released, 0.0, 1e-9);
    EXPECT_TRUE(r.energy_matches);
}

TEST_F(PaymentSessionTest, NeverChargesMoreThanTheHold) {
    ASSERT_TRUE(session.start(5));
    session.charge(3600 * 5, 10);
    session.finish();
    EXPECT_NEAR(terminal.cardBalance(), 95.0, 1e-9);
}

TEST_F(PaymentSessionTest, BudgetNotExhaustedWhenChargeFitsInHold) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    EXPECT_FALSE(session.budgetExhausted());
}

// --- Battery full ---

TEST_F(PaymentSessionTest, FullBatteryStopsAndReleasesUnusedHold) {
    ASSERT_TRUE(session.start(100));    // $100 would buy 200 kWh
    session.charge(3600 * 10, 10);      // battery only has 40 kWh of room
    const Receipt r = session.finish();

    EXPECT_TRUE(battery.isFull());
    EXPECT_NEAR(r.energy_after_kwh, 50.0, 1e-9);
    EXPECT_NEAR(r.amount_charged, 20.0, 1e-9);   // 40 kWh * $0.50
    EXPECT_NEAR(r.amount_released, 80.0, 1e-9);
}

// --- Failures ---

TEST_F(PaymentSessionTest, CaptureFailureIsReportedAsUnpaid) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    terminal.setOffline(true);  // connection drops mid-session
    const Receipt r = session.finish();

    EXPECT_FALSE(r.paid);
    EXPECT_DOUBLE_EQ(r.amount_charged, 0);
    EXPECT_NEAR(r.energy_delivered_kwh, 10.0, 1e-9);  // energy was still delivered
    EXPECT_FALSE(terminal.isAuthorized());            // hold is not left dangling
    EXPECT_NEAR(terminal.cardBalance(), 100.0, 1e-9);
}

TEST_F(PaymentSessionTest, FaultMidSessionStillSettlesWhatWasDelivered) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    charger.reportFault();
    const Receipt r = session.finish();

    EXPECT_TRUE(r.paid);
    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
    EXPECT_EQ(charger.state(), ChargerState::Available);  // fault cleared
}

TEST_F(PaymentSessionTest, ChargeBeforeStartDoesNothing) {
    session.charge(3600, 10);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 10.0);
}

TEST_F(PaymentSessionTest, NegativeChargeInputsThrow) {
    ASSERT_TRUE(session.start(50));
    EXPECT_THROW(session.charge(-1, 10), std::invalid_argument);
    EXPECT_THROW(session.charge(1, -10), std::invalid_argument);
}

TEST_F(PaymentSessionTest, CanRunASecondSessionAfterTheFirst) {
    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);
    session.finish();

    ASSERT_TRUE(session.start(50));
    const Receipt r = session.finish();
    EXPECT_NEAR(r.energy_before_kwh, 20.0, 1e-9);  // picks up where the battery is now
}
