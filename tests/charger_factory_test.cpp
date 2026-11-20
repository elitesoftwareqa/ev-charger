#include <gtest/gtest.h>

#include <stdexcept>

#include "abb_charger.h"
#include "battery.h"
#include "btc_charger.h"
#include "charger_repository.h"
#include "charger_factory.h"
#include "constants.h"
#include "payment_session.h"
#include "payment_terminal.h"

class ChargerFactoryTest : public ::testing::Test {
protected:
    InMemoryChargerRepository repository;  // stub: no SQLite involved
    void SetUp() override { repository.seedDefaults(); }

    // Power delivered at `soc` percent when the car requests a huge amount.
    static double powerAt(Charger& charger, double soc) {
        Battery battery(200.0, soc);
        charger.plugIn(battery);
        const double power = charger.deliveredPowerKw(1000);
        charger.startCharging();
        charger.stopCharging();
        charger.unplug();
        return power;
    }
};

// --- Building from the seeded rows ---

TEST_F(ChargerFactoryTest, GenericRowBuildsAPlainCharger) {
    const auto charger = makeCharger(repository, constants::kSeedGenericName);
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 11.0);
    EXPECT_DOUBLE_EQ(charger->pricePerKwh(), constants::kGenericPricePerKwh);
    EXPECT_DOUBLE_EQ(charger->sessionFee(), 0.0);
    EXPECT_EQ(dynamic_cast<AbbCharger*>(charger.get()), nullptr);
    EXPECT_EQ(dynamic_cast<BtcCharger*>(charger.get()), nullptr);
}

TEST_F(ChargerFactoryTest, AbbRowBuildsAnAbbCharger) {
    const auto charger = makeCharger(repository, constants::kSeedAbbName);
    EXPECT_NE(dynamic_cast<AbbCharger*>(charger.get()), nullptr);
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 150.0);
    EXPECT_DOUBLE_EQ(powerAt(*charger, 50), 150.0);
    EXPECT_DOUBLE_EQ(powerAt(*charger, 90), 75.0);  // tapered
}

TEST_F(ChargerFactoryTest, BtcRowBuildsABtcCharger) {
    const auto charger = makeCharger(repository, constants::kSeedBtcName);
    EXPECT_NE(dynamic_cast<BtcCharger*>(charger.get()), nullptr);
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 60.0);
    EXPECT_DOUBLE_EQ(charger->sessionFee(), 1.0);
    EXPECT_DOUBLE_EQ(powerAt(*charger, 90), 60.0);  // no taper
}

// --- Values really come from the database ---

TEST_F(ChargerFactoryTest, CustomAbbValuesFlowThrough) {
    ChargerSpec s;
    s.name = "ABB fast";
    s.type = constants::kChargerTypeAbb;
    s.max_power_kw = 200;
    s.price_per_kwh = 0.55;
    s.taper_above_percent = 50;
    s.taper_factor = 0.25;
    repository.add(s);

    const auto charger = makeCharger(repository, "ABB fast");
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 200.0);
    EXPECT_DOUBLE_EQ(charger->pricePerKwh(), 0.55);
    EXPECT_DOUBLE_EQ(powerAt(*charger, 40), 200.0);
    EXPECT_DOUBLE_EQ(powerAt(*charger, 60), 50.0);  // 200 kW * 0.25 above 50%
}

TEST_F(ChargerFactoryTest, CustomBtcFeeFlowsThrough) {
    ChargerSpec s;
    s.name = "BTC premium";
    s.type = constants::kChargerTypeBtc;
    s.max_power_kw = 90;
    s.price_per_kwh = 0.50;
    s.session_fee = 2.5;
    repository.add(s);

    const auto charger = makeCharger(repository, "BTC premium");
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 90.0);
    EXPECT_DOUBLE_EQ(charger->sessionFee(), 2.5);
}

TEST_F(ChargerFactoryTest, PriceChangeInTheRepositoryAffectsNewChargersOnly) {
    const auto before = makeCharger(repository, constants::kSeedAbbName);
    repository.updatePrice(constants::kSeedAbbName, 0.99);
    const auto after = makeCharger(repository, constants::kSeedAbbName);

    EXPECT_DOUBLE_EQ(before->pricePerKwh(), constants::kAbbDefaultPricePerKwh);
    EXPECT_DOUBLE_EQ(after->pricePerKwh(), 0.99);
}

TEST_F(ChargerFactoryTest, RemovedChargerCannotBeBuilt) {
    repository.remove(constants::kSeedBtcName);
    EXPECT_THROW(makeCharger(repository, constants::kSeedBtcName), std::invalid_argument);
}

// --- Errors ---

TEST_F(ChargerFactoryTest, UnknownNameThrows) {
    EXPECT_THROW(makeCharger(repository, "nope"), std::invalid_argument);
}

TEST_F(ChargerFactoryTest, UnknownTypeInASpecThrows) {
    ChargerSpec s;
    s.name = "x";
    s.type = "tesla";
    s.max_power_kw = 10;
    s.price_per_kwh = 1;
    EXPECT_THROW(makeCharger(s), std::invalid_argument);
}

TEST_F(ChargerFactoryTest, ChargerClassesRejectSpecsOfTheWrongType) {
    const ChargerSpec btc = *repository.find(constants::kSeedBtcName);
    const ChargerSpec abb = *repository.find(constants::kSeedAbbName);
    EXPECT_THROW(AbbCharger{btc}, std::invalid_argument);
    EXPECT_THROW(BtcCharger{abb}, std::invalid_argument);
}

TEST_F(ChargerFactoryTest, AbbRejectsBadTaperValues) {
    ChargerSpec s = *repository.find(constants::kSeedAbbName);
    s.taper_factor = 0;
    EXPECT_THROW(AbbCharger{s}, std::invalid_argument);
    s.taper_factor = 1.5;
    EXPECT_THROW(AbbCharger{s}, std::invalid_argument);
    s = *repository.find(constants::kSeedAbbName);
    s.taper_above_percent = 101;
    EXPECT_THROW(AbbCharger{s}, std::invalid_argument);
}

TEST_F(ChargerFactoryTest, BtcRejectsNegativeFee) {
    ChargerSpec s = *repository.find(constants::kSeedBtcName);
    s.session_fee = -1;
    EXPECT_THROW(BtcCharger{s}, std::invalid_argument);
}

TEST_F(ChargerFactoryTest, ChargerRejectsNonPositivePowerFromASpec) {
    ChargerSpec s = *repository.find(constants::kSeedGenericName);
    s.max_power_kw = 0;
    EXPECT_THROW(makeCharger(s), std::invalid_argument);
}

// --- End to end: repository -> charger -> paid session ---

TEST_F(ChargerFactoryTest, RepositoryChargerRunsAPaidSession) {
    const auto charger = makeCharger(repository, constants::kSeedBtcName);
    Battery battery(200.0, 20.0);
    PaymentTerminal card(100.0);
    PaymentSession session(card, *charger, battery);

    ASSERT_TRUE(session.start(5));  // $1 fee + $4 of energy at $0.40/kWh = 10 kWh
    session.charge(3600, 60);
    const Receipt r = session.finish();

    EXPECT_NEAR(r.energy_delivered_kwh, 10.0, 1e-9);
    EXPECT_NEAR(r.amount_charged, 5.0, 1e-9);
}

TEST_F(ChargerFactoryTest, ChangingThePriceInTheRepositoryChangesTheBill) {
    repository.updatePrice(constants::kSeedGenericName, 1.00);
    const auto charger = makeCharger(repository, constants::kSeedGenericName);
    Battery battery(200.0, 20.0);
    PaymentTerminal card(100.0);
    PaymentSession session(card, *charger, battery);

    ASSERT_TRUE(session.start(50));
    session.charge(3600, 10);  // 10 kWh at $1.00
    EXPECT_NEAR(session.finish().amount_charged, 10.0, 1e-9);
}
