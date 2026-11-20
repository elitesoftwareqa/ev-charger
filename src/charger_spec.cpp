#include "charger_spec.h"

std::vector<ChargerSpec> defaultChargerSpecs() {
    using namespace constants;
    return {
        {kSeedGenericName, kChargerTypeGeneric, kGenericMaxPowerKw, kGenericPricePerKwh,
         kNoSessionFee, kNoTaperAbovePercent, kNoTaperFactor},
        {kSeedAbbName, kChargerTypeAbb, kAbbMaxPowerKw, kAbbDefaultPricePerKwh, kNoSessionFee,
         kAbbTaperAbovePercent, kAbbTaperFactor},
        {kSeedBtcName, kChargerTypeBtc, kBtcMaxPowerKw, kBtcDefaultPricePerKwh, kBtcSessionFee,
         kNoTaperAbovePercent, kNoTaperFactor},
    };
}
