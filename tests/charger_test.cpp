#include <gtest/gtest.h>

#include <stdexcept>

#include "battery.h"
#include "charger.h"

class ChargerTest : public ::testing::Test {
protected:
    // 11 kW charger, $0.50 per kWh
    Charger charger{11.0, 0.50};

    void startSession() {
        ASSERT_TRUE(charger.plugIn());
        ASSERT_TRUE(charger.startCharging());
    }
};

// --- Construction ---

TEST_F(ChargerTest, StartsAvailable) {
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(ChargerTest, RejectsNonPositiveMaxPower) {
    EXPECT_THROW(Charger(0, 0.5), std::invalid_argument);
    EXPECT_THROW(Charger(-5, 0.5), std::invalid_argument);
}

TEST_F(ChargerTest, RejectsNonPositivePrice) {
    EXPECT_THROW(Charger(11, 0), std::invalid_argument);
}

// --- State transitions ---

TEST_F(ChargerTest, PlugInMovesToConnected) {
    EXPECT_TRUE(charger.plugIn());
    EXPECT_EQ(charger.state(), ChargerState::Connected);
}

TEST_F(ChargerTest, CannotPlugInTwice) {
    charger.plugIn();
    EXPECT_FALSE(charger.plugIn());
}

TEST_F(ChargerTest, CannotStartChargingWithoutCar) {
    EXPECT_FALSE(charger.startCharging());
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(ChargerTest, StartChargingMovesToCharging) {
    charger.plugIn();
    EXPECT_TRUE(charger.startCharging());
    EXPECT_EQ(charger.state(), ChargerState::Charging);
}

TEST_F(ChargerTest, StopChargingMovesToFinished) {
    startSession();
    EXPECT_TRUE(charger.stopCharging());
    EXPECT_EQ(charger.state(), ChargerState::Finished);
}

TEST_F(ChargerTest, CannotStopWhenNotCharging) {
    charger.plugIn();
    EXPECT_FALSE(charger.stopCharging());
}

TEST_F(ChargerTest, CannotUnplugWhileCharging) {
    startSession();
    EXPECT_FALSE(charger.unplug());
    EXPECT_EQ(charger.state(), ChargerState::Charging);
}

TEST_F(ChargerTest, UnplugAfterFinishedMovesToAvailable) {
    startSession();
    charger.stopCharging();
    EXPECT_TRUE(charger.unplug());
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

// --- Energy and cost ---

TEST_F(ChargerTest, NoEnergyBeforeCharging) {
    charger.plugIn();
    charger.charge(3600, 7);
    EXPECT_DOUBLE_EQ(charger.energyDeliveredKwh(), 0);
}

TEST_F(ChargerTest, DeliversEnergyOverTime) {
    startSession();
    charger.charge(3600, 7);  // 7 kW for 1 hour
    EXPECT_NEAR(charger.energyDeliveredKwh(), 7.0, 1e-9);
}

TEST_F(ChargerTest, EnergyAccumulatesAcrossTicks) {
    startSession();
    charger.charge(1800, 10);  // 5 kWh
    charger.charge(1800, 10);  // 5 kWh
    EXPECT_NEAR(charger.energyDeliveredKwh(), 10.0, 1e-9);
}

TEST_F(ChargerTest, PowerIsCappedAtChargerMax) {
    startSession();
    charger.charge(3600, 50);  // car asks 50 kW, charger caps at 11
    EXPECT_NEAR(charger.energyDeliveredKwh(), 11.0, 1e-9);
}

TEST_F(ChargerTest, NoEnergyAfterStopped) {
    startSession();
    charger.charge(3600, 10);
    charger.stopCharging();
    charger.charge(3600, 10);
    EXPECT_NEAR(charger.energyDeliveredKwh(), 10.0, 1e-9);
}

TEST_F(ChargerTest, CostIsEnergyTimesPrice) {
    startSession();
    charger.charge(3600, 10);  // 10 kWh * $0.50
    EXPECT_NEAR(charger.sessionCost(), 5.0, 1e-9);
}

TEST_F(ChargerTest, NegativeInputsThrow) {
    startSession();
    EXPECT_THROW(charger.charge(-1, 5), std::invalid_argument);
    EXPECT_THROW(charger.charge(1, -5), std::invalid_argument);
}

TEST_F(ChargerTest, UnplugResetsSession) {
    startSession();
    charger.charge(3600, 10);
    charger.stopCharging();
    charger.unplug();
    EXPECT_DOUBLE_EQ(charger.energyDeliveredKwh(), 0);
    EXPECT_DOUBLE_EQ(charger.sessionCost(), 0);
}

// --- Faults ---

TEST_F(ChargerTest, FaultStopsCharging) {
    startSession();
    charger.reportFault();
    EXPECT_EQ(charger.state(), ChargerState::Faulted);
    charger.charge(3600, 10);
    EXPECT_DOUBLE_EQ(charger.energyDeliveredKwh(), 0);
}

TEST_F(ChargerTest, FaultedChargerCannotPlugIn) {
    charger.reportFault();
    EXPECT_FALSE(charger.plugIn());
}

TEST_F(ChargerTest, ClearFaultReturnsToAvailable) {
    charger.reportFault();
    EXPECT_TRUE(charger.clearFault());
    EXPECT_EQ(charger.state(), ChargerState::Available);
}

TEST_F(ChargerTest, ClearFaultWhenNotFaultedFails) {
    EXPECT_FALSE(charger.clearFault());
}

// --- Charger connected to a Battery ---

class ChargerBatteryTest : public ::testing::Test {
protected:
    Charger charger{11.0, 0.50};  // 11 kW, $0.50 per kWh
    Battery battery{50.0, 20.0};  // 50 kWh battery at 20% = 10 kWh

    void startSession() {
        ASSERT_TRUE(charger.plugIn(battery));
        ASSERT_TRUE(charger.startCharging());
    }
};

TEST_F(ChargerBatteryTest, BatteryValueBeforeAndAfterCharge) {
    startSession();

    const double energy_before = battery.energyKwh();
    const double soc_before = battery.stateOfChargePercent();
    EXPECT_DOUBLE_EQ(energy_before, 10.0);
    EXPECT_DOUBLE_EQ(soc_before, 20.0);

    charger.charge(3600, 10);  // 10 kW for 1 hour = 10 kWh

    const double energy_after = battery.energyKwh();
    const double soc_after = battery.stateOfChargePercent();
    EXPECT_NEAR(energy_after, 20.0, 1e-9);
    EXPECT_NEAR(soc_after, 40.0, 1e-9);
    EXPECT_NEAR(energy_after - energy_before, charger.energyDeliveredKwh(), 1e-9);
    EXPECT_GT(soc_after, soc_before);
}

TEST_F(ChargerBatteryTest, ChargeLevelRisesFromLowToNormal) {
    Battery low_battery(50.0, 10.0);
    ASSERT_EQ(low_battery.chargeLevel(), ChargeLevel::Low);
    charger.plugIn(low_battery);
    charger.startCharging();
    charger.charge(3600, 10);  // +10 kWh -> 30 %
    EXPECT_EQ(low_battery.chargeLevel(), ChargeLevel::Normal);
}

TEST_F(ChargerBatteryTest, BatteryUnchangedBeforeChargingStarts) {
    charger.plugIn(battery);  // plugged in, but startCharging() not called
    charger.charge(3600, 10);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 10.0);
}

TEST_F(ChargerBatteryTest, ChargerPowerCapLimitsBatteryGain) {
    startSession();
    charger.charge(3600, 50);  // car asks 50 kW, charger caps at 11 kW
    EXPECT_NEAR(battery.energyKwh(), 21.0, 1e-9);
}

TEST_F(ChargerBatteryTest, ChargingStopsAutomaticallyWhenBatteryIsFull) {
    startSession();
    charger.charge(3600 * 10, 10);  // far more than the 40 kWh of room
    EXPECT_TRUE(battery.isFull());
    EXPECT_EQ(battery.chargeLevel(), ChargeLevel::Full);
    EXPECT_EQ(charger.state(), ChargerState::Finished);
}

TEST_F(ChargerBatteryTest, DeliveredEnergyAndCostOnlyCountWhatFit) {
    startSession();
    charger.charge(3600 * 10, 10);  // wants 100 kWh, only 40 fit
    EXPECT_NEAR(charger.energyDeliveredKwh(), 40.0, 1e-9);
    EXPECT_NEAR(charger.sessionCost(), 20.0, 1e-9);  // 40 kWh * $0.50
}

TEST_F(ChargerBatteryTest, NoMoreEnergyAfterAutoFinish) {
    startSession();
    charger.charge(3600 * 10, 10);
    charger.charge(3600, 10);
    EXPECT_NEAR(charger.energyDeliveredKwh(), 40.0, 1e-9);
}

TEST_F(ChargerBatteryTest, UnplugDetachesBattery) {
    startSession();
    charger.charge(3600, 10);
    charger.stopCharging();
    charger.unplug();
    const double energy_at_unplug = battery.energyKwh();

    charger.plugIn();  // plug in again, this time without a battery
    charger.startCharging();
    charger.charge(3600, 10);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), energy_at_unplug);
}

TEST_F(ChargerBatteryTest, CannotPlugInBatteryWhenNotAvailable) {
    charger.reportFault();
    EXPECT_FALSE(charger.plugIn(battery));
}
