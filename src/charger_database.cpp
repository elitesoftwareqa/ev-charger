#include "charger_database.h"

#include <sqlite3.h>

#include <string>

#include "constants.h"

namespace {

// Owns one prepared statement. Values are always bound as parameters, never
// pasted into the SQL text, so names containing quotes cannot change a query.
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw DatabaseError(sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind(int index, double value) { sqlite3_bind_double(stmt_, index, value); }

    // Steps once; returns true if a row is available.
    bool nextRow() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw DatabaseError(sqlite3_errmsg(db_));
    }

    // For statements that return no rows (INSERT, UPDATE, DELETE).
    void run() {
        if (nextRow()) throw DatabaseError("unexpected row from a non-query statement");
    }

    std::string text(int column) const {
        const unsigned char* t = sqlite3_column_text(stmt_, column);
        return t ? reinterpret_cast<const char*>(t) : std::string();
    }
    double real(int column) const { return sqlite3_column_double(stmt_, column); }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

const char* const kSelectColumns =
    "name, type, max_power_kw, price_per_kwh, session_fee, taper_above_percent, taper_factor";

ChargerSpec readSpec(const Statement& s) {
    ChargerSpec spec;
    spec.name = s.text(0);
    spec.type = s.text(1);
    spec.max_power_kw = s.real(2);
    spec.price_per_kwh = s.real(3);
    spec.session_fee = s.real(4);
    spec.taper_above_percent = s.real(5);
    spec.taper_factor = s.real(6);
    return spec;
}

std::string createTableSql() {
    using namespace constants;
    return std::string(
               "CREATE TABLE IF NOT EXISTS chargers ("
               " id INTEGER PRIMARY KEY AUTOINCREMENT,"
               " name TEXT NOT NULL UNIQUE CHECK (length(name) > 0),"
               " type TEXT NOT NULL CHECK (type IN ('") +
           kChargerTypeGeneric + "', '" + kChargerTypeAbb + "', '" + kChargerTypeBtc +
           "')),"
           " max_power_kw REAL NOT NULL CHECK (max_power_kw > 0),"
           " price_per_kwh REAL NOT NULL CHECK (price_per_kwh > 0),"
           " session_fee REAL NOT NULL DEFAULT 0 CHECK (session_fee >= 0),"
           " taper_above_percent REAL NOT NULL DEFAULT 100"
           "   CHECK (taper_above_percent BETWEEN 0 AND 100),"
           " taper_factor REAL NOT NULL DEFAULT 1"
           "   CHECK (taper_factor > 0 AND taper_factor <= 1)"
           ");";
}

}  // namespace

ChargerDatabase::ChargerDatabase(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw DatabaseError("cannot open database '" + path + "': " + message);
    }
    try {
        Statement(db_, createTableSql()).run();
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

ChargerDatabase::~ChargerDatabase() { sqlite3_close(db_); }

void ChargerDatabase::add(const ChargerSpec& spec) {
    Statement s(db_, std::string("INSERT INTO chargers (") + kSelectColumns +
                         ") VALUES (?, ?, ?, ?, ?, ?, ?)");
    s.bind(1, spec.name);
    s.bind(2, spec.type);
    s.bind(3, spec.max_power_kw);
    s.bind(4, spec.price_per_kwh);
    s.bind(5, spec.session_fee);
    s.bind(6, spec.taper_above_percent);
    s.bind(7, spec.taper_factor);
    s.run();
}

std::optional<ChargerSpec> ChargerDatabase::find(const std::string& name) const {
    Statement s(db_, std::string("SELECT ") + kSelectColumns + " FROM chargers WHERE name = ?");
    s.bind(1, name);
    if (!s.nextRow()) return std::nullopt;
    return readSpec(s);
}

std::vector<ChargerSpec> ChargerDatabase::all() const {
    Statement s(db_, std::string("SELECT ") + kSelectColumns + " FROM chargers ORDER BY id");
    std::vector<ChargerSpec> result;
    while (s.nextRow()) result.push_back(readSpec(s));
    return result;
}

std::size_t ChargerDatabase::count() const {
    Statement s(db_, "SELECT COUNT(*) FROM chargers");
    s.nextRow();
    return static_cast<std::size_t>(s.real(0));
}

bool ChargerDatabase::updatePrice(const std::string& name, double price_per_kwh) {
    Statement s(db_, "UPDATE chargers SET price_per_kwh = ? WHERE name = ?");
    s.bind(1, price_per_kwh);
    s.bind(2, name);
    s.run();
    return sqlite3_changes(db_) > 0;
}

bool ChargerDatabase::remove(const std::string& name) {
    Statement s(db_, "DELETE FROM chargers WHERE name = ?");
    s.bind(1, name);
    s.run();
    return sqlite3_changes(db_) > 0;
}
