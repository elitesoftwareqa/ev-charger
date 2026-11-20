#pragma once

#include <gmock/gmock.h>

#include "charger_repository.h"

// A gmock mock of the repository, for tests that want to check *how* the code
// talks to storage (which calls, with what arguments, how many times) or that
// need storage to fail on purpose.
class MockChargerRepository : public ChargerRepository {
public:
    MOCK_METHOD(void, add, (const ChargerSpec& spec), (override));
    MOCK_METHOD(std::optional<ChargerSpec>, find, (const std::string& name), (const, override));
    MOCK_METHOD(std::vector<ChargerSpec>, all, (), (const, override));
    MOCK_METHOD(std::size_t, count, (), (const, override));
    MOCK_METHOD(bool, updatePrice, (const std::string& name, double price_per_kwh), (override));
    MOCK_METHOD(bool, remove, (const std::string& name), (override));
};
