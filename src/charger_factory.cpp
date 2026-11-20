#include "charger_factory.h"

#include <stdexcept>

#include "abb_charger.h"
#include "btc_charger.h"
#include "constants.h"

std::unique_ptr<Charger> makeCharger(const ChargerSpec& spec) {
    if (spec.type == constants::kChargerTypeGeneric) {
        return std::make_unique<Charger>(spec.max_power_kw, spec.price_per_kwh);
    }
    if (spec.type == constants::kChargerTypeAbb) return std::make_unique<AbbCharger>(spec);
    if (spec.type == constants::kChargerTypeBtc) return std::make_unique<BtcCharger>(spec);
    throw std::invalid_argument("unknown charger type '" + spec.type + "'");
}

std::unique_ptr<Charger> makeCharger(const ChargerRepository& repository,
                                     const std::string& name) {
    const std::optional<ChargerSpec> spec = repository.find(name);
    if (!spec) throw std::invalid_argument("no charger named '" + name + "' in the repository");
    return makeCharger(*spec);
}
