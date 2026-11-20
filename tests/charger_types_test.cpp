#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <ostream>
#include <string>

#include "abb_charger.h"
#include "battery.h"
#include "btc_charger.h"
#include "charger.h"
#include "payment_session.h"
#include "payment_terminal.h"

// ---------------------------------------------------------------------------
// Contract tests: every charger type must pass all of these.
// ---------------------------------------------------------------------------

struct ChargerType {
    std::string name;
    std::function<std::unique_ptr<Charger>()> make;
};

// Lets gtest show the parameter by name in test listings.
inline void PrintTo(const ChargerType& k, std::ostream* os) { *os << k.name; }

class AnyChargerTest : public ::testing::TestWithParam<ChargerType> {
protected:
    std::unique_ptr<Charger> charger = GetParam().make();
    Battery battery{200.0, 20.0};  // big battery at 20% = 40 kWh (below any taper)
};

TEST_P(AnyChargerTest, StartsAvailable) {
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(AnyChargerTest, FollowsTheNormalStateFlow) {
    EXPECT_TRUE(charger->plugIn(battery));
    EXPECT_EQ(charger->state(), ChargerState::Connected);
    EXPECT_TRUE(charger->startCharging());
    EXPECT_EQ(charger->state(), ChargerState::Charging);
    EXPECT_TRUE(charger->stopCharging());
    EXPECT_EQ(charger->state(), ChargerState::Finished);
    EXPECT_TRUE(charger->unplug());
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

TEST_P(AnyChargerTest, CannotStartChargingWithoutCar) {
    EXPECT_FALSE(charger->startCharging());
}

TEST_P(AnyChargerTest, NoEnergyBeforeChargingStarts) {
    charger->plugIn(battery);
    charger->charge(3600, 50);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 40.0);
    EXPECT_DOUBLE_EQ(charger->sessionCost(), 0.0);
}

TEST_P(AnyChargerTest, BatteryValueBeforeAndAfterCharge) {
    charger->plugIn(battery);
    charger->startCharging();

    const double before = battery.energyKwh();
    charger->charge(600, 1000);  // car requests far more than any charger gives
    const double after = battery.energyKwh();

    EXPECT_GT(after, before);
    EXPECT_NEAR(after - before, charger->energyDeliveredKwh(), 1e-9);
}

TEST_P(AnyChargerTest, PowerNeverExceedsChargerMaximum) {
    charger->plugIn(battery);
    charger->startCharging();
    charger->charge(3600, 1000);
    EXPECT_LE(charger->energyDeliveredKwh(), charger->maxPowerKw() * 1.0 + 1e-9);
}

TEST_P(AnyChargerTest, DeliversExactlyWhatCarRequestsWhenBelowMax) {
    charger->plugIn(battery);
    charger->startCharging();
    charger->charge(3600, 10);  // 10 kW for 1 hour
    EXPECT_NEAR(charger->energyDeliveredKwh(), 10.0, 1e-9);
}

TEST_P(AnyChargerTest, CostCoversAtLeastTheEnergy) {
    charger->plugIn(battery);
    charger->startCharging();
    charger->charge(3600, 10);
    EXPECT_GE(charger->sessionCost() + 1e-9,
              charger->energyDeliveredKwh() * charger->pricePerKwh());
}

TEST_P(AnyChargerTest, StopsAutomaticallyWhenBatteryIsFull) {
    Battery small(10.0, 50.0);
    charger->plugIn(small);
    charger->startCharging();
    charger->charge(3600 * 10, 1000);
    EXPECT_TRUE(small.isFull());
    EXPECT_EQ(charger->state(), ChargerState::Finished);
}

TEST_P(AnyChargerTest, UnplugResetsTheSession) {
    charger->plugIn(battery);
    charger->startCharging();
    charger->charge(3600, 10);
    charger->stopCharging();
    charger->unplug();
    EXPECT_DOUBLE_EQ(charger->energyDeliveredKwh(), 0);
    EXPECT_DOUBLE_EQ(charger->sessionCost(), 0);
}

TEST_P(AnyChargerTest, FaultStopsChargingAndCanBeCleared) {
    charger->plugIn(battery);
    charger->startCharging();
    charger->reportFault();
    EXPECT_EQ(charger->state(), ChargerState::Faulted);
    charger->charge(3600, 10);
    EXPECT_DOUBLE_EQ(charger->energyDeliveredKwh(), 0);
    EXPECT_TRUE(charger->clearFault());
    EXPECT_EQ(charger->state(), ChargerState::Available);
}

// --- The paid-session flow must also work on every charger type ---

TEST_P(AnyChargerTest, PaidSessionReceiptMatchesBattery) {
    PaymentTerminal terminal(100.0);
    PaymentSession session(terminal, *charger, battery);

    ASSERT_TRUE(session.start(50));
    session.charge(600, 20);
    const Receipt r = session.finish();

    EXPECT_NEAR(r.energy_before_kwh, 40.0, 1e-9);
    EXPECT_GT(r.energy_after_kwh, r.energy_before_kwh);
    EXPECT_TRUE(r.energy_matches);
    EXPECT_TRUE(r.paid);
    EXPECT_NEAR(terminal.cardBalance(), 100.0 - r.amount_charged, 1e-9);
}

TEST_P(AnyChargerTest, PaidSessionNeverBillsAboveTheHold) {
    PaymentTerminal terminal(100.0);
    PaymentSession session(terminal, *charger, battery);

    ASSERT_TRUE(session.start(5));
    session.charge(3600 * 2, 1000);  // wants far more than $5 buys
    EXPECT_TRUE(session.budgetExhausted());
    const Receipt r = session.finish();

    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
    EXPECT_TRUE(r.energy_matches);
}

INSTANTIATE_TEST_SUITE_P(
    AllChargers, AnyChargerTest,
    ::testing::Values(
        ChargerType{"Generic", [] { return std::make_unique<Charger>(11.0, 0.50); }},
        ChargerType{"ABB", [] { return std::make_unique<AbbCharger>(); }},
        ChargerType{"BTC", [] { return std::make_unique<BtcCharger>(); }}),
    [](const ::testing::TestParamInfo<ChargerType>& info) { return info.param.name; });

// ---------------------------------------------------------------------------
// ABB-specific behavior
// ---------------------------------------------------------------------------

TEST(AbbChargerTest, HasExpectedLimitsAndPrice) {
    AbbCharger abb;
    EXPECT_DOUBLE_EQ(abb.maxPowerKw(), 150.0);
    EXPECT_DOUBLE_EQ(abb.pricePerKwh(), 0.45);
}

TEST(AbbChargerTest, CustomPriceIsUsed) {
    EXPECT_DOUBLE_EQ(AbbCharger(0.60).pricePerKwh(), 0.60);
}

TEST(AbbChargerTest, DeliversFullPowerBelowTaperThreshold) {
    AbbCharger abb;
    Battery battery(200.0, 50.0);
    abb.plugIn(battery);
    abb.startCharging();
    abb.charge(600, 1000);  // 150 kW for 10 minutes = 25 kWh
    EXPECT_NEAR(abb.energyDeliveredKwh(), 25.0, 1e-9);
}

TEST(AbbChargerTest, TapersPowerAboveEightyPercent) {
    AbbCharger abb;
    Battery battery(200.0, 90.0);  // above the 80% threshold
    abb.plugIn(battery);
    abb.startCharging();
    abb.charge(600, 1000);  // tapered to 75 kW for 10 minutes = 12.5 kWh
    EXPECT_NEAR(abb.energyDeliveredKwh(), 12.5, 1e-9);
}

TEST(AbbChargerTest, NoTaperExactlyAtEightyPercent) {
    AbbCharger abb;
    Battery battery(200.0, 80.0);  // the taper starts strictly above 80%
    abb.plugIn(battery);
    abb.startCharging();
    abb.charge(600, 1000);
    EXPECT_NEAR(abb.energyDeliveredKwh(), 25.0, 1e-9);
}

TEST(AbbChargerTest, TaperNeverRaisesAModestRequest) {
    AbbCharger abb;
    Battery battery(200.0, 90.0);
    abb.plugIn(battery);
    abb.startCharging();
    abb.charge(3600, 20);  // car asks only 20 kW, below the 75 kW taper cap
    EXPECT_NEAR(abb.energyDeliveredKwh(), 20.0, 1e-9);
}

TEST(AbbChargerTest, ChargesFasterThanBtcOnTheSameBattery) {
    AbbCharger abb;
    BtcCharger btc;
    Battery a(200.0, 20.0), b(200.0, 20.0);
    abb.plugIn(a);
    abb.startCharging();
    btc.plugIn(b);
    btc.startCharging();

    abb.charge(600, 1000);
    btc.charge(600, 1000);
    EXPECT_GT(a.energyKwh(), b.energyKwh());
}

TEST(AbbChargerTest, HasNoSessionFee) {
    AbbCharger abb;
    Battery battery(200.0, 20.0);
    abb.plugIn(battery);
    abb.startCharging();
    EXPECT_DOUBLE_EQ(abb.sessionFee(), 0.0);
    EXPECT_DOUBLE_EQ(abb.sessionCost(), 0.0);  // nothing delivered yet
}

// ---------------------------------------------------------------------------
// BTC-specific behavior
// ---------------------------------------------------------------------------

TEST(BtcChargerTest, HasExpectedLimitsAndPrice) {
    BtcCharger btc;
    EXPECT_DOUBLE_EQ(btc.maxPowerKw(), 60.0);
    EXPECT_DOUBLE_EQ(btc.pricePerKwh(), 0.40);
}

TEST(BtcChargerTest, CapsPowerAtSixtyKw) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    btc.startCharging();
    btc.charge(3600, 150);  // car asks 150 kW, BTC gives 60
    EXPECT_NEAR(btc.energyDeliveredKwh(), 60.0, 1e-9);
}

TEST(BtcChargerTest, DoesNotTaperAtHighBatteryLevels) {
    BtcCharger btc;
    Battery battery(200.0, 95.0);
    btc.plugIn(battery);
    btc.startCharging();
    btc.charge(300, 1000);  // 60 kW for 5 minutes = 5 kWh (room: 10 kWh)
    EXPECT_NEAR(btc.energyDeliveredKwh(), 5.0, 1e-9);
}

TEST(BtcChargerTest, NoFeeBeforeChargingStarts) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    EXPECT_DOUBLE_EQ(btc.sessionCost(), 0.0);
}

TEST(BtcChargerTest, FeeAppliesAsSoonAsChargingStarts) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    btc.startCharging();
    EXPECT_DOUBLE_EQ(btc.sessionFee(), 1.0);
    EXPECT_DOUBLE_EQ(btc.sessionCost(), 1.0);
}

TEST(BtcChargerTest, CostIsFeePlusEnergy) {
    BtcCharger btc;  // $0.40/kWh + $1.00 fee
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    btc.startCharging();
    btc.charge(3600, 10);  // 10 kWh -> $4.00 + $1.00
    EXPECT_NEAR(btc.sessionCost(), 5.0, 1e-9);
}

TEST(BtcChargerTest, FeeIsChargedOnlyOncePerSession) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    btc.startCharging();
    btc.charge(1800, 10);
    btc.charge(1800, 10);  // 10 kWh total in two steps
    EXPECT_NEAR(btc.sessionCost(), 5.0, 1e-9);
}

TEST(BtcChargerTest, FeeResetsAfterUnplug) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    btc.plugIn(battery);
    btc.startCharging();
    btc.stopCharging();
    btc.unplug();
    EXPECT_DOUBLE_EQ(btc.sessionCost(), 0.0);
}

TEST(BtcChargerTest, PaymentHoldCoversTheFee) {
    BtcCharger btc;  // $1 fee + $0.40/kWh
    Battery battery(200.0, 20.0);
    PaymentTerminal terminal(100.0);
    PaymentSession session(terminal, btc, battery);

    ASSERT_TRUE(session.start(5));  // $5 hold = $1 fee + $4 of energy = 10 kWh
    session.charge(3600, 60);       // would be 60 kWh
    EXPECT_TRUE(session.budgetExhausted());
    const Receipt r = session.finish();

    EXPECT_NEAR(r.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
}

TEST(BtcChargerTest, StartedSessionWithNoEnergyStillPaysTheFee) {
    BtcCharger btc;
    Battery battery(200.0, 20.0);
    PaymentTerminal terminal(100.0);
    PaymentSession session(terminal, btc, battery);

    ASSERT_TRUE(session.start(20));
    const Receipt r = session.finish();
    EXPECT_NEAR(r.amount_charged, 1.0, 1e-9);
    EXPECT_NEAR(terminal.cardBalance(), 99.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Polymorphism: code written against Charger& works with any type
// ---------------------------------------------------------------------------

TEST(ChargerPolymorphismTest, BaseReferenceUsesTheDerivedBehavior) {
    BtcCharger btc;
    Charger& base = btc;
    EXPECT_DOUBLE_EQ(base.maxPowerKw(), 60.0);
    EXPECT_DOUBLE_EQ(base.sessionFee(), 1.0);
    EXPECT_DOUBLE_EQ(base.deliveredPowerKw(1000), 60.0);
}

TEST(ChargerPolymorphismTest, DeletingThroughBasePointerIsSafe) {
    std::unique_ptr<Charger> p = std::make_unique<AbbCharger>();
    EXPECT_DOUBLE_EQ(p->maxPowerKw(), 150.0);
}  // destructor runs through the virtual base destructor
