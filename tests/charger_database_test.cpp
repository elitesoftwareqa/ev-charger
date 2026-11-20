#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "abb_charger.h"
#include "btc_charger.h"
#include "charger_database.h"
#include "charger_factory.h"
#include "constants.h"

class ChargerDatabaseTest : public ::testing::Test {
protected:
    ChargerDatabase db{":memory:"};

    static ChargerSpec customSpec(const std::string& name = "Custom") {
        ChargerSpec s;
        s.name = name;
        s.type = constants::kChargerTypeGeneric;
        s.max_power_kw = 22.0;
        s.price_per_kwh = 0.35;
        return s;
    }
};

// --- Empty and seeded ---

TEST_F(ChargerDatabaseTest, NewDatabaseIsEmpty) {
    EXPECT_EQ(db.count(), 0u);
    EXPECT_TRUE(db.all().empty());
}

TEST_F(ChargerDatabaseTest, SeedDefaultsAddsTheThreeChargers) {
    db.seedDefaults();
    ASSERT_EQ(db.count(), 3u);
    const auto rows = db.all();
    EXPECT_EQ(rows[0].name, constants::kSeedGenericName);
    EXPECT_EQ(rows[1].name, constants::kSeedAbbName);
    EXPECT_EQ(rows[2].name, constants::kSeedBtcName);
}

TEST_F(ChargerDatabaseTest, SeedDefaultsCanRunTwiceWithoutDuplicates) {
    db.seedDefaults();
    db.seedDefaults();
    EXPECT_EQ(db.count(), 3u);
}

TEST_F(ChargerDatabaseTest, SeedDefaultsNeverOverwritesEdits) {
    db.seedDefaults();
    ASSERT_TRUE(db.updatePrice(constants::kSeedAbbName, 0.99));
    db.seedDefaults();
    EXPECT_DOUBLE_EQ(db.find(constants::kSeedAbbName)->price_per_kwh, 0.99);
}

TEST_F(ChargerDatabaseTest, SeededRowsMatchTheDefaultsInConstants) {
    db.seedDefaults();
    const auto abb = db.find(constants::kSeedAbbName);
    ASSERT_TRUE(abb.has_value());
    EXPECT_EQ(abb->type, constants::kChargerTypeAbb);
    EXPECT_DOUBLE_EQ(abb->max_power_kw, constants::kAbbMaxPowerKw);
    EXPECT_DOUBLE_EQ(abb->price_per_kwh, constants::kAbbDefaultPricePerKwh);
    EXPECT_DOUBLE_EQ(abb->taper_above_percent, constants::kAbbTaperAbovePercent);
    EXPECT_DOUBLE_EQ(abb->taper_factor, constants::kAbbTaperFactor);

    const auto btc = db.find(constants::kSeedBtcName);
    ASSERT_TRUE(btc.has_value());
    EXPECT_DOUBLE_EQ(btc->session_fee, constants::kBtcSessionFee);
}

TEST_F(ChargerDatabaseTest, DatabaseChargersBehaveLikeTheBuiltInDefaults) {
    db.seedDefaults();
    const auto from_db = makeCharger(db, constants::kSeedAbbName);
    AbbCharger built_in;
    EXPECT_DOUBLE_EQ(from_db->maxPowerKw(), built_in.maxPowerKw());
    EXPECT_DOUBLE_EQ(from_db->pricePerKwh(), built_in.pricePerKwh());
    EXPECT_DOUBLE_EQ(from_db->sessionFee(), built_in.sessionFee());
}

// --- Find / all ---

TEST_F(ChargerDatabaseTest, FindUnknownReturnsNothing) {
    EXPECT_FALSE(db.find("nope").has_value());
}

TEST_F(ChargerDatabaseTest, AddThenFindReturnsStoredValues) {
    ChargerSpec s = customSpec();
    s.session_fee = 2.5;
    db.add(s);

    const auto found = db.find("Custom");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->type, constants::kChargerTypeGeneric);
    EXPECT_DOUBLE_EQ(found->max_power_kw, 22.0);
    EXPECT_DOUBLE_EQ(found->price_per_kwh, 0.35);
    EXPECT_DOUBLE_EQ(found->session_fee, 2.5);
}

TEST_F(ChargerDatabaseTest, AllReturnsRowsInInsertionOrder) {
    db.add(customSpec("Zulu"));
    db.add(customSpec("Alpha"));
    const auto rows = db.all();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, "Zulu");
    EXPECT_EQ(rows[1].name, "Alpha");
}

// --- Validation (the table's CHECK constraints) ---

TEST_F(ChargerDatabaseTest, DuplicateNameIsRejected) {
    db.add(customSpec());
    EXPECT_THROW(db.add(customSpec()), DatabaseError);
    EXPECT_EQ(db.count(), 1u);
}

TEST_F(ChargerDatabaseTest, EmptyNameIsRejected) {
    EXPECT_THROW(db.add(customSpec("")), DatabaseError);
}

TEST_F(ChargerDatabaseTest, NonPositivePowerIsRejected) {
    ChargerSpec s = customSpec();
    s.max_power_kw = 0;
    EXPECT_THROW(db.add(s), DatabaseError);
    s.max_power_kw = -5;
    EXPECT_THROW(db.add(s), DatabaseError);
}

TEST_F(ChargerDatabaseTest, NonPositivePriceIsRejected) {
    ChargerSpec s = customSpec();
    s.price_per_kwh = 0;
    EXPECT_THROW(db.add(s), DatabaseError);
}

TEST_F(ChargerDatabaseTest, NegativeFeeIsRejected) {
    ChargerSpec s = customSpec();
    s.session_fee = -1;
    EXPECT_THROW(db.add(s), DatabaseError);
}

TEST_F(ChargerDatabaseTest, UnknownTypeIsRejected) {
    ChargerSpec s = customSpec();
    s.type = "tesla";
    EXPECT_THROW(db.add(s), DatabaseError);
}

TEST_F(ChargerDatabaseTest, TaperValuesOutOfRangeAreRejected) {
    ChargerSpec s = customSpec();
    s.taper_factor = 0;
    EXPECT_THROW(db.add(s), DatabaseError);
    s.taper_factor = 1.5;
    EXPECT_THROW(db.add(s), DatabaseError);
    s = customSpec();
    s.taper_above_percent = 120;
    EXPECT_THROW(db.add(s), DatabaseError);
    EXPECT_EQ(db.count(), 0u);
}

// --- Update / remove ---

TEST_F(ChargerDatabaseTest, UpdatePriceChangesTheStoredValue) {
    db.add(customSpec());
    EXPECT_TRUE(db.updatePrice("Custom", 0.80));
    EXPECT_DOUBLE_EQ(db.find("Custom")->price_per_kwh, 0.80);
}

TEST_F(ChargerDatabaseTest, UpdatePriceOfUnknownChargerReturnsFalse) {
    EXPECT_FALSE(db.updatePrice("nope", 0.80));
}

TEST_F(ChargerDatabaseTest, InvalidPriceUpdateIsRejectedAndKeepsTheOldPrice) {
    db.add(customSpec());
    EXPECT_THROW(db.updatePrice("Custom", -1), DatabaseError);
    EXPECT_DOUBLE_EQ(db.find("Custom")->price_per_kwh, 0.35);
}

TEST_F(ChargerDatabaseTest, RemoveDeletesTheRow) {
    db.add(customSpec());
    EXPECT_TRUE(db.remove("Custom"));
    EXPECT_FALSE(db.find("Custom").has_value());
    EXPECT_EQ(db.count(), 0u);
}

TEST_F(ChargerDatabaseTest, RemoveUnknownChargerReturnsFalse) {
    EXPECT_FALSE(db.remove("nope"));
}

// --- Safety ---

TEST_F(ChargerDatabaseTest, NamesWithSqlCharactersAreStoredLiterally) {
    const std::string nasty = "x'); DROP TABLE chargers; --";
    db.add(customSpec(nasty));
    db.add(customSpec("Other"));  // the table still exists
    EXPECT_EQ(db.count(), 2u);
    ASSERT_TRUE(db.find(nasty).has_value());
    EXPECT_EQ(db.find(nasty)->name, nasty);
}

// --- Files ---

TEST(ChargerDatabaseFileTest, DataSurvivesClosingAndReopening) {
    const std::string path = ::testing::TempDir() + "charger_db_persistence_test.db";
    std::remove(path.c_str());
    {
        ChargerDatabase first(path);
        first.seedDefaults();
        first.updatePrice(constants::kSeedBtcName, 0.77);
    }
    {
        ChargerDatabase second(path);
        EXPECT_EQ(second.count(), 3u);
        EXPECT_DOUBLE_EQ(second.find(constants::kSeedBtcName)->price_per_kwh, 0.77);
    }
    std::remove(path.c_str());
}

TEST(ChargerDatabaseFileTest, OpeningAnUnwritablePathThrows) {
    EXPECT_THROW(ChargerDatabase("/nonexistent-folder/charger.db"), DatabaseError);
}

TEST(ChargerDatabaseFileTest, OpeningAFileThatIsNotADatabaseThrows) {
    const std::string path = ::testing::TempDir() + "charger_db_not_a_database_test.db";
    {
        std::ofstream out(path);
        out << "this is plain text, definitely not a SQLite database file, "
               "padded so that it is longer than a SQLite header..............................";
    }
    EXPECT_THROW(ChargerDatabase{path}, DatabaseError);
    std::remove(path.c_str());
}
