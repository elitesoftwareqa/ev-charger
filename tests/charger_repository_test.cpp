#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>

#include "abb_charger.h"
#include "battery.h"
#include "btc_charger.h"
#include "charger_database.h"
#include "charger_factory.h"
#include "charger_repository.h"
#include "constants.h"
#include "mock_charger_repository.h"

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Throw;

namespace {

ChargerSpec customSpec(const std::string& name = "Custom") {
    ChargerSpec s;
    s.name = name;
    s.type = constants::kChargerTypeGeneric;
    s.max_power_kw = 22.0;
    s.price_per_kwh = 0.35;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Contract tests: the real SQLite database and the in-memory stub must behave
// the same way. If the stub drifts from the real thing, these fail.
// ---------------------------------------------------------------------------

struct RepositoryKind {
    std::string name;
    std::function<std::unique_ptr<ChargerRepository>()> make;
};

// Lets gtest show the parameter by name in test listings.
inline void PrintTo(const RepositoryKind& k, std::ostream* os) { *os << k.name; }

class RepositoryContractTest : public ::testing::TestWithParam<RepositoryKind> {
protected:
    std::unique_ptr<ChargerRepository> repo = GetParam().make();
};

TEST_P(RepositoryContractTest, StartsEmpty) {
    EXPECT_EQ(repo->count(), 0u);
    EXPECT_TRUE(repo->all().empty());
    EXPECT_FALSE(repo->find("anything").has_value());
}

TEST_P(RepositoryContractTest, SeedDefaultsAddsTheDefaultChargersOnce) {
    repo->seedDefaults();
    repo->seedDefaults();
    ASSERT_EQ(repo->count(), 3u);
    const auto rows = repo->all();
    EXPECT_EQ(rows[0].name, constants::kSeedGenericName);
    EXPECT_EQ(rows[1].name, constants::kSeedAbbName);
    EXPECT_EQ(rows[2].name, constants::kSeedBtcName);
}

TEST_P(RepositoryContractTest, SeedDefaultsDoesNotOverwriteEdits) {
    repo->seedDefaults();
    repo->updatePrice(constants::kSeedAbbName, 0.99);
    repo->seedDefaults();
    EXPECT_DOUBLE_EQ(repo->find(constants::kSeedAbbName)->price_per_kwh, 0.99);
}

TEST_P(RepositoryContractTest, AddThenFindRoundTripsEveryField) {
    ChargerSpec s = customSpec();
    s.type = constants::kChargerTypeAbb;
    s.session_fee = 2.5;
    s.taper_above_percent = 70;
    s.taper_factor = 0.4;
    repo->add(s);

    const auto found = repo->find("Custom");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->type, s.type);
    EXPECT_DOUBLE_EQ(found->max_power_kw, s.max_power_kw);
    EXPECT_DOUBLE_EQ(found->price_per_kwh, s.price_per_kwh);
    EXPECT_DOUBLE_EQ(found->session_fee, s.session_fee);
    EXPECT_DOUBLE_EQ(found->taper_above_percent, s.taper_above_percent);
    EXPECT_DOUBLE_EQ(found->taper_factor, s.taper_factor);
}

TEST_P(RepositoryContractTest, AllKeepsInsertionOrder) {
    repo->add(customSpec("Zulu"));
    repo->add(customSpec("Alpha"));
    const auto rows = repo->all();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].name, "Zulu");
    EXPECT_EQ(rows[1].name, "Alpha");
}

TEST_P(RepositoryContractTest, RejectsDuplicateNames) {
    repo->add(customSpec());
    EXPECT_THROW(repo->add(customSpec()), RepositoryError);
    EXPECT_EQ(repo->count(), 1u);
}

TEST_P(RepositoryContractTest, RejectsInvalidSpecs) {
    auto bad = [&](auto change) {
        ChargerSpec s = customSpec();
        change(s);
        EXPECT_THROW(repo->add(s), RepositoryError);
    };
    bad([](ChargerSpec& s) { s.name = ""; });
    bad([](ChargerSpec& s) { s.type = "tesla"; });
    bad([](ChargerSpec& s) { s.max_power_kw = 0; });
    bad([](ChargerSpec& s) { s.max_power_kw = -5; });
    bad([](ChargerSpec& s) { s.price_per_kwh = 0; });
    bad([](ChargerSpec& s) { s.session_fee = -1; });
    bad([](ChargerSpec& s) { s.taper_above_percent = 101; });
    bad([](ChargerSpec& s) { s.taper_above_percent = -1; });
    bad([](ChargerSpec& s) { s.taper_factor = 0; });
    bad([](ChargerSpec& s) { s.taper_factor = 1.5; });
    EXPECT_EQ(repo->count(), 0u);
}

TEST_P(RepositoryContractTest, UpdatePriceChangesTheStoredValue) {
    repo->add(customSpec());
    EXPECT_TRUE(repo->updatePrice("Custom", 0.80));
    EXPECT_DOUBLE_EQ(repo->find("Custom")->price_per_kwh, 0.80);
}

TEST_P(RepositoryContractTest, UpdatePriceOfUnknownChargerReturnsFalse) {
    EXPECT_FALSE(repo->updatePrice("nope", 0.80));
}

TEST_P(RepositoryContractTest, InvalidPriceUpdateThrowsAndKeepsTheOldPrice) {
    repo->add(customSpec());
    EXPECT_THROW(repo->updatePrice("Custom", -1), RepositoryError);
    EXPECT_DOUBLE_EQ(repo->find("Custom")->price_per_kwh, 0.35);
}

TEST_P(RepositoryContractTest, RemoveDeletesTheRow) {
    repo->add(customSpec());
    EXPECT_TRUE(repo->remove("Custom"));
    EXPECT_FALSE(repo->find("Custom").has_value());
    EXPECT_FALSE(repo->remove("Custom"));
}

TEST_P(RepositoryContractTest, NamesWithSqlCharactersAreJustText) {
    const std::string nasty = "x'); DROP TABLE chargers; --";
    repo->add(customSpec(nasty));
    repo->add(customSpec("Other"));
    EXPECT_EQ(repo->count(), 2u);
    EXPECT_TRUE(repo->find(nasty).has_value());
}

INSTANTIATE_TEST_SUITE_P(
    SqliteAndStub, RepositoryContractTest,
    ::testing::Values(
        RepositoryKind{"Sqlite",
                       [] { return std::make_unique<ChargerDatabase>(":memory:"); }},
        RepositoryKind{"InMemoryStub",
                       [] { return std::make_unique<InMemoryChargerRepository>(); }}),
    [](const ::testing::TestParamInfo<RepositoryKind>& info) { return info.param.name; });

// ---------------------------------------------------------------------------
// Mock-based tests: check the conversation with storage, and storage failures.
// StrictMock fails the test on any call we did not expect.
// ---------------------------------------------------------------------------

TEST(MockRepositoryTest, FactoryAsksForExactlyTheNamedCharger) {
    StrictMock<MockChargerRepository> mock;
    ChargerSpec spec = *defaultChargerSpecs().begin();
    spec.name = "Lobby charger";
    EXPECT_CALL(mock, find("Lobby charger")).Times(1).WillOnce(Return(spec));

    const auto charger = makeCharger(mock, "Lobby charger");
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), spec.max_power_kw);
}

TEST(MockRepositoryTest, FactoryBuildsWhateverTheRepositoryReturns) {
    // No database at all: the mock returns a simulated ABB row.
    StrictMock<MockChargerRepository> mock;
    ChargerSpec spec;
    spec.name = "Mock ABB";
    spec.type = constants::kChargerTypeAbb;
    spec.max_power_kw = 350;
    spec.price_per_kwh = 0.70;
    spec.taper_above_percent = 60;
    spec.taper_factor = 0.2;
    EXPECT_CALL(mock, find("Mock ABB")).WillOnce(Return(spec));

    const auto charger = makeCharger(mock, "Mock ABB");
    EXPECT_NE(dynamic_cast<AbbCharger*>(charger.get()), nullptr);
    EXPECT_DOUBLE_EQ(charger->maxPowerKw(), 350.0);
    EXPECT_DOUBLE_EQ(charger->pricePerKwh(), 0.70);

    Battery battery(200.0, 80.0);  // above the 60% taper threshold
    charger->plugIn(battery);
    EXPECT_DOUBLE_EQ(charger->deliveredPowerKw(1000), 70.0);  // 350 kW * 0.2
}

TEST(MockRepositoryTest, FactoryThrowsWhenTheRepositoryHasNoSuchCharger) {
    StrictMock<MockChargerRepository> mock;
    EXPECT_CALL(mock, find("ghost")).WillOnce(Return(std::nullopt));
    EXPECT_THROW(makeCharger(mock, "ghost"), std::invalid_argument);
}

TEST(MockRepositoryTest, StorageFailurePropagatesToTheCaller) {
    StrictMock<MockChargerRepository> mock;
    EXPECT_CALL(mock, find(_)).WillOnce(Throw(RepositoryError("disk is on fire")));
    EXPECT_THROW(makeCharger(mock, "ABB"), RepositoryError);
}

TEST(MockRepositoryTest, FactoryNeverWritesToTheRepository) {
    // StrictMock would fail if makeCharger called add/update/remove.
    StrictMock<MockChargerRepository> mock;
    EXPECT_CALL(mock, find(_)).WillOnce(Return(defaultChargerSpecs()[1]));
    makeCharger(mock, constants::kSeedAbbName);
}

TEST(MockRepositoryTest, SeedDefaultsAddsOnlyWhatIsMissing) {
    StrictMock<MockChargerRepository> mock;
    const auto defaults = defaultChargerSpecs();
    // Generic and BTC are missing; ABB already exists.
    EXPECT_CALL(mock, find(defaults[0].name)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(mock, find(defaults[1].name)).WillOnce(Return(defaults[1]));
    EXPECT_CALL(mock, find(defaults[2].name)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(mock, add(::testing::Field(&ChargerSpec::name, defaults[0].name))).Times(1);
    EXPECT_CALL(mock, add(::testing::Field(&ChargerSpec::name, defaults[2].name))).Times(1);

    mock.seedDefaults();
}

TEST(MockRepositoryTest, SeedDefaultsStopsAndReportsIfStorageFails) {
    StrictMock<MockChargerRepository> mock;
    EXPECT_CALL(mock, find(_)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(mock, add(_)).WillOnce(Throw(RepositoryError("read-only database")));
    EXPECT_THROW(mock.seedDefaults(), RepositoryError);
}

// ---------------------------------------------------------------------------
// Stub-specific checks
// ---------------------------------------------------------------------------

TEST(InMemoryRepositoryTest, ValidationHelperIsUsedByTheStub) {
    InMemoryChargerRepository stub;
    ChargerSpec s = customSpec();
    s.max_power_kw = 0;
    EXPECT_THROW(validateChargerSpec(s), RepositoryError);
    EXPECT_THROW(stub.add(s), RepositoryError);
}

TEST(InMemoryRepositoryTest, DatabaseErrorIsARepositoryError) {
    // Code written against the interface can catch RepositoryError for any backend.
    try {
        ChargerDatabase("/nonexistent-folder/x.db");
        FAIL() << "expected an exception";
    } catch (const RepositoryError& e) {
        SUCCEED() << e.what();
    }
}
