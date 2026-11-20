#include "charger_repository.h"

#include <algorithm>

#include "constants.h"

void ChargerRepository::seedDefaults() {
    for (const ChargerSpec& spec : defaultChargerSpecs()) {
        if (!find(spec.name)) add(spec);
    }
}

void validateChargerSpec(const ChargerSpec& spec) {
    using namespace constants;
    if (spec.name.empty()) throw RepositoryError("charger name must not be empty");
    if (spec.type != kChargerTypeGeneric && spec.type != kChargerTypeAbb &&
        spec.type != kChargerTypeBtc) {
        throw RepositoryError("unknown charger type '" + spec.type + "'");
    }
    if (spec.max_power_kw <= 0) throw RepositoryError("max power must be positive");
    if (spec.price_per_kwh <= 0) throw RepositoryError("price must be positive");
    if (spec.session_fee < 0) throw RepositoryError("session fee must not be negative");
    if (spec.taper_above_percent < kPercentEmpty || spec.taper_above_percent > kPercentFull) {
        throw RepositoryError("taper threshold must be between 0 and 100 percent");
    }
    if (spec.taper_factor <= 0 || spec.taper_factor > 1) {
        throw RepositoryError("taper factor must be in (0, 1]");
    }
}

void InMemoryChargerRepository::add(const ChargerSpec& spec) {
    validateChargerSpec(spec);
    if (find(spec.name)) throw RepositoryError("charger '" + spec.name + "' already exists");
    rows_.push_back(spec);
}

std::optional<ChargerSpec> InMemoryChargerRepository::find(const std::string& name) const {
    const auto it = std::find_if(rows_.begin(), rows_.end(),
                                 [&](const ChargerSpec& s) { return s.name == name; });
    if (it == rows_.end()) return std::nullopt;
    return *it;
}

std::vector<ChargerSpec> InMemoryChargerRepository::all() const { return rows_; }

std::size_t InMemoryChargerRepository::count() const { return rows_.size(); }

bool InMemoryChargerRepository::updatePrice(const std::string& name, double price_per_kwh) {
    const auto it = std::find_if(rows_.begin(), rows_.end(),
                                 [&](const ChargerSpec& s) { return s.name == name; });
    if (it == rows_.end()) return false;
    ChargerSpec updated = *it;
    updated.price_per_kwh = price_per_kwh;
    validateChargerSpec(updated);  // throws before anything is changed
    *it = updated;
    return true;
}

bool InMemoryChargerRepository::remove(const std::string& name) {
    const auto it = std::find_if(rows_.begin(), rows_.end(),
                                 [&](const ChargerSpec& s) { return s.name == name; });
    if (it == rows_.end()) return false;
    rows_.erase(it);
    return true;
}
