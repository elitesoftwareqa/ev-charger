#pragma once

// Shared helpers for the two "master" end-to-end test suites
// (payter_master_test.cpp and app_master_test.cpp).

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "charger.h"
#include "charger_factory.h"
#include "charger_repository.h"
#include "constants.h"
#include "payment_session.h"

struct ChargerKind {
    std::string name;
    std::function<std::unique_ptr<Charger>()> make;
};

// Lets gtest show the parameter by name in test listings.
inline void PrintTo(const ChargerKind& k, std::ostream* os) { *os << k.name; }

// Builds a charger the way production code does: read its row from a
// repository and let the factory pick the class. The repository here is the
// in-memory stub, so these end-to-end tests never touch SQLite.
inline std::unique_ptr<Charger> chargerFromRepository(const std::string& name) {
    InMemoryChargerRepository repository;
    repository.seedDefaults();
    return makeCharger(repository, name);
}

// Every charger type the master suites run against, loaded from the repository.
inline std::vector<ChargerKind> allChargerKinds() {
    return {
        {"Generic", [] { return chargerFromRepository(constants::kSeedGenericName); }},
        {"ABB", [] { return chargerFromRepository(constants::kSeedAbbName); }},
        {"BTC", [] { return chargerFromRepository(constants::kSeedBtcName); }},
    };
}

inline std::string chargerKindName(const ::testing::TestParamInfo<ChargerKind>& info) {
    return info.param.name;
}

// Rules every receipt must obey, whatever triggered the charge.
inline void expectReceiptConsistent(const Receipt& r, double hold) {
    // The battery gained exactly what the charger says it delivered.
    EXPECT_TRUE(r.energy_matches);
    EXPECT_NEAR(r.energy_after_kwh - r.energy_before_kwh, r.energy_delivered_kwh, 1e-9);
    // Energy never goes backwards.
    EXPECT_GE(r.energy_after_kwh, r.energy_before_kwh);
    // Money: never charge above the hold, and charged + released == hold.
    EXPECT_LE(r.amount_charged, hold + 1e-9);
    EXPECT_GE(r.amount_charged, 0.0);
    EXPECT_NEAR(r.amount_charged + r.amount_released, hold, 1e-9);
}
