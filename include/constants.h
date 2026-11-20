#pragma once

// Every tunable number and fixed value used by the project, in one place.
// All charger specs below are simulated values.

namespace constants {

// --- Units and conversions ---------------------------------------------------
inline constexpr double kSecondsPerHour = 3600.0;

// --- Battery -------------------------------------------------------------------
inline constexpr double kPercentEmpty = 0.0;
inline constexpr double kPercentFull = 100.0;
// Below this state of charge (but above empty) a battery counts as "Low".
inline constexpr double kLowChargePercent = 20.0;

// --- Generic charger -------------------------------------------------------------
inline constexpr double kGenericMaxPowerKw = 11.0;
inline constexpr double kGenericPricePerKwh = 0.30;
// Fixed fee for a session on a charger that has none.
inline constexpr double kNoSessionFee = 0.0;
// "No taper" values, used for chargers whose power never tapers.
inline constexpr double kNoTaperAbovePercent = 100.0;
inline constexpr double kNoTaperFactor = 1.0;

// --- ABB-style DC fast charger -----------------------------------------------------
inline constexpr double kAbbMaxPowerKw = 150.0;
inline constexpr double kAbbDefaultPricePerKwh = 0.45;
// Power tapers once the car battery is above this state of charge...
inline constexpr double kAbbTaperAbovePercent = 80.0;
// ...to this fraction of the charger's maximum power.
inline constexpr double kAbbTaperFactor = 0.5;

// --- BTC-style charger -------------------------------------------------------------
inline constexpr double kBtcMaxPowerKw = 60.0;
inline constexpr double kBtcDefaultPricePerKwh = 0.40;
inline constexpr double kBtcSessionFee = 1.00;

// --- Payment session -----------------------------------------------------------------
// How close the battery's gain and the charger's delivered energy must be to
// count as matching (floating-point tolerance).
inline constexpr double kEnergyMatchToleranceKwh = 1e-9;

// --- Charger database -------------------------------------------------------------------
inline constexpr char kDefaultDatabasePath[] = "chargers.db";

// Values of the `type` column. They select which Charger class is built.
inline constexpr char kChargerTypeGeneric[] = "generic";
inline constexpr char kChargerTypeAbb[] = "abb";
inline constexpr char kChargerTypeBtc[] = "btc";

// Names of the rows inserted by ChargerDatabase::seedDefaults().
inline constexpr char kSeedGenericName[] = "Generic 11 kW";
inline constexpr char kSeedAbbName[] = "ABB";
inline constexpr char kSeedBtcName[] = "BTC";

// --- Demo program (src/main.cpp) -------------------------------------------------------
namespace demo {
inline constexpr double kBatteryCapacityKwh = 60.0;
inline constexpr double kBatteryStartPercent = 20.0;
inline constexpr double kCarRequestKw = 150.0;
inline constexpr double kChargeSeconds = 600.0;
}  // namespace demo

}  // namespace constants
