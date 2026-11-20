#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "charger_repository.h"

struct sqlite3;  // from sqlite3.h; kept out of this header on purpose

// Thrown when SQLite reports an error (bad path, constraint violation, ...).
class DatabaseError : public RepositoryError {
public:
    using RepositoryError::RepositoryError;
};

// A SQLite database of charger models.
//
//   chargers(id, name UNIQUE, type, max_power_kw, price_per_kwh,
//            session_fee, taper_above_percent, taper_factor)
//
// The table's CHECK constraints reject nonsense (non-positive power or price,
// negative fee, taper values out of range, unknown type), so bad data cannot
// get in even if it is written by another tool.
class ChargerDatabase : public ChargerRepository {
public:
    // Opens `path` (created if missing) or ":memory:" for a throwaway database,
    // and creates the table if it does not exist. Throws DatabaseError on failure.
    explicit ChargerDatabase(const std::string& path);
    ~ChargerDatabase() override;

    ChargerDatabase(const ChargerDatabase&) = delete;
    ChargerDatabase& operator=(const ChargerDatabase&) = delete;

    // Throws DatabaseError on a duplicate name or an invalid value.
    void add(const ChargerSpec& spec) override;

    std::optional<ChargerSpec> find(const std::string& name) const override;
    std::vector<ChargerSpec> all() const override;  // in insertion order
    std::size_t count() const override;
    bool updatePrice(const std::string& name, double price_per_kwh) override;
    bool remove(const std::string& name) override;

private:
    sqlite3* db_ = nullptr;
};
