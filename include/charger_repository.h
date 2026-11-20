#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "charger_spec.h"

// Thrown by any repository when it cannot do what was asked
// (invalid values, duplicate name, storage failure, ...).
class RepositoryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Where charger models are stored. The rest of the program (the factory, the
// demo) only talks to this interface, so the real SQLite database can be
// swapped for a stub or a mock in tests.
class ChargerRepository {
public:
    virtual ~ChargerRepository() = default;

    // Throws RepositoryError on a duplicate name or an invalid value.
    virtual void add(const ChargerSpec& spec) = 0;

    virtual std::optional<ChargerSpec> find(const std::string& name) const = 0;
    virtual std::vector<ChargerSpec> all() const = 0;  // in insertion order
    virtual std::size_t count() const = 0;

    // Returns false if no charger has that name. Throws RepositoryError if the
    // price is invalid (the stored value is left unchanged).
    virtual bool updatePrice(const std::string& name, double price_per_kwh) = 0;

    // Returns false if no charger has that name.
    virtual bool remove(const std::string& name) = 0;

    // Adds each default charger (see defaultChargerSpecs()) that is not already
    // present. Never overwrites an existing row. Works on any implementation.
    void seedDefaults();
};

// The rules every repository enforces (the SQLite table has matching CHECK
// constraints). Throws RepositoryError describing the first problem found.
void validateChargerSpec(const ChargerSpec& spec);

// A plain in-memory repository for tests: same behavior as the SQLite one,
// no database involved. Also usable as a simple stub.
class InMemoryChargerRepository : public ChargerRepository {
public:
    void add(const ChargerSpec& spec) override;
    std::optional<ChargerSpec> find(const std::string& name) const override;
    std::vector<ChargerSpec> all() const override;
    std::size_t count() const override;
    bool updatePrice(const std::string& name, double price_per_kwh) override;
    bool remove(const std::string& name) override;

private:
    std::vector<ChargerSpec> rows_;
};
