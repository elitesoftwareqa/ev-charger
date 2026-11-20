#pragma once

#include <memory>
#include <string>

#include "charger.h"
#include "charger_repository.h"
#include "charger_spec.h"

// Builds the right Charger class for a spec: "generic" -> Charger,
// "abb" -> AbbCharger, "btc" -> BtcCharger.
// Throws std::invalid_argument for an unknown type or invalid values.
std::unique_ptr<Charger> makeCharger(const ChargerSpec& spec);

// Looks `name` up in the repository (SQLite, a stub, a mock, ...) and builds that
// charger. Throws std::invalid_argument if there is no such charger.
std::unique_ptr<Charger> makeCharger(const ChargerRepository& repository, const std::string& name);
