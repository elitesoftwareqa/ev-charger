#include <gtest/gtest.h>

#include <stdexcept>

#include "battery.h"

class BatteryTest : public ::testing::Test {
protected:
    // 60 kWh battery at 50%
    Battery battery{60.0, 50.0};
};

// --- Construction ---

TEST_F(BatteryTest, ReportsInitialState) {
    EXPECT_DOUBLE_EQ(battery.capacityKwh(), 60);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 30);
    EXPECT_DOUBLE_EQ(battery.stateOfChargePercent(), 50);
}

TEST_F(BatteryTest, RejectsNonPositiveCapacity) {
    EXPECT_THROW(Battery(0, 50), std::invalid_argument);
    EXPECT_THROW(Battery(-10, 50), std::invalid_argument);
}

TEST_F(BatteryTest, RejectsOutOfRangeStateOfCharge) {
    EXPECT_THROW(Battery(60, -1), std::invalid_argument);
    EXPECT_THROW(Battery(60, 101), std::invalid_argument);
}

TEST_F(BatteryTest, AcceptsBoundaryStatesOfCharge) {
    EXPECT_NO_THROW(Battery(60, 0));
    EXPECT_NO_THROW(Battery(60, 100));
}

// --- Adding energy ---

TEST_F(BatteryTest, AddEnergyIncreasesCharge) {
    EXPECT_DOUBLE_EQ(battery.addEnergy(6), 6);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 36);
    EXPECT_DOUBLE_EQ(battery.stateOfChargePercent(), 60);
}

TEST_F(BatteryTest, AddEnergyCapsAtCapacity) {
    const double accepted = battery.addEnergy(100);  // only 30 kWh of room
    EXPECT_DOUBLE_EQ(accepted, 30);
    EXPECT_DOUBLE_EQ(battery.energyKwh(), 60);
    EXPECT_DOUBLE_EQ(battery.stateOfChargePercent(), 100);
}

TEST_F(BatteryTest, AddEnergyToFullBatteryAcceptsNothing) {
    battery.addEnergy(100);
    EXPECT_DOUBLE_EQ(battery.addEnergy(5), 0);
}

TEST_F(BatteryTest, AddNegativeEnergyThrows) {
    EXPECT_THROW(battery.addEnergy(-1), std::invalid_argument);
}

// --- Full check ---

TEST_F(BatteryTest, NotFullInitially) {
    EXPECT_FALSE(battery.isFull());
}

TEST_F(BatteryTest, FullAfterFilling) {
    battery.addEnergy(30);
    EXPECT_TRUE(battery.isFull());
}

// --- Time to full ---

TEST_F(BatteryTest, HoursToFullAtConstantPower) {
    EXPECT_NEAR(battery.hoursToFull(10), 3.0, 1e-9);  // 30 kWh / 10 kW
}

TEST_F(BatteryTest, HoursToFullIsZeroWhenFull) {
    battery.addEnergy(100);
    EXPECT_DOUBLE_EQ(battery.hoursToFull(10), 0);
}

TEST_F(BatteryTest, HoursToFullRejectsNonPositivePower) {
    EXPECT_THROW(battery.hoursToFull(0), std::invalid_argument);
    EXPECT_THROW(battery.hoursToFull(-5), std::invalid_argument);
}

// --- Charge level ---

TEST(BatteryChargeLevelTest, EmptyAtZeroPercent) {
    EXPECT_EQ(Battery(60, 0).chargeLevel(), ChargeLevel::Empty);
}

TEST(BatteryChargeLevelTest, LowBelowTwentyPercent) {
    EXPECT_EQ(Battery(60, 5).chargeLevel(), ChargeLevel::Low);
    EXPECT_EQ(Battery(60, 19.9).chargeLevel(), ChargeLevel::Low);
}

TEST(BatteryChargeLevelTest, NormalFromTwentyPercent) {
    EXPECT_EQ(Battery(60, 20).chargeLevel(), ChargeLevel::Normal);
    EXPECT_EQ(Battery(60, 99).chargeLevel(), ChargeLevel::Normal);
}

TEST(BatteryChargeLevelTest, FullAtHundredPercent) {
    EXPECT_EQ(Battery(60, 100).chargeLevel(), ChargeLevel::Full);
}

TEST(BatteryChargeLevelTest, LevelChangesAsEnergyIsAdded) {
    Battery b(100, 0);
    EXPECT_EQ(b.chargeLevel(), ChargeLevel::Empty);
    b.addEnergy(10);
    EXPECT_EQ(b.chargeLevel(), ChargeLevel::Low);
    b.addEnergy(40);
    EXPECT_EQ(b.chargeLevel(), ChargeLevel::Normal);
    b.addEnergy(50);
    EXPECT_EQ(b.chargeLevel(), ChargeLevel::Full);
}
