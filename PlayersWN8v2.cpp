#include <iostream>
#include <string>
#include <libpq-fe.h>
#include <sys/time.h>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <ctime>

#include "./lib/Pgsql.h"
#include "./lib/GetPlayersStats.h"
#include "./lib/GetPlayersVehicles.h"

using namespace std;

// Konstanty
constexpr const char* MASTER_QUERY = "SELECT account_id FROM players_all";
constexpr int BATCH_SIZE = 100;
constexpr int SAVE_THRESHOLD = 2500;
constexpr size_t NUM_THREADS = 4; // Počet vlákien pre paralelizáciu

// Typy
using timestamp_t = unsigned long long;
using wn8value = unordered_map<int, float>;

// Thread-safe fronta pre account_id
class ThreadSafeQueue {
private:
    vector<int> data;
    mutable mutex mtx;
    size_t read_pos = 0;

public:
    void push_bulk(vector<int>&& ids) {
        lock_guard<mutex> lock(mtx);
        data = move(ids);
        read_pos = 0;
    }

    bool get_batch(vector<int>& batch, size_t batch_size) {
        lock_guard<mutex> lock(mtx);
        if (read_pos >= data.size()) return false;

        size_t end = min(read_pos + batch_size, data.size());
        batch.clear();
        batch.reserve(end - read_pos);
        batch.insert(batch.end(), data.begin() + read_pos, data.begin() + end);
        read_pos = end;
        return true;
    }

    size_t size() const {
        lock_guard<mutex> lock(mtx);
        return data.size() - read_pos;
    }

    bool empty() const {
        lock_guard<mutex> lock(mtx);
        return read_pos >= data.size();
    }
};

// Thread-safe buffer pre WN8 hodnoty
class WN8Buffer {
private:
    wn8value data;
    mutable mutex mtx;

public:
    void insert(const wn8value& values) {
        lock_guard<mutex> lock(mtx);
        data.insert(values.begin(), values.end());
    }

    size_t size() const {
        lock_guard<mutex> lock(mtx);
        return data.size();
    }

    wn8value extract_all() {
        lock_guard<mutex> lock(mtx);
        wn8value result = move(data);
        data.clear();
        return result;
    }

    wn8value extract_if_threshold(size_t threshold) {
        lock_guard<mutex> lock(mtx);
        if (data.size() >= threshold) {
            wn8value result = move(data);
            data.clear();
            return result;
        }
        return {};
    }
};

// Meranie času
static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, nullptr);
    return now.tv_usec + static_cast<timestamp_t>(now.tv_sec) * 1000000;
}

// Získanie včerajšieho dátumu
string GetYesterdayDate() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);

    char buffer[11];
    tm_info->tm_mday--;
    mktime(tm_info); // Normalizácia dátumu
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm_info);

    return string(buffer);
}

// WN8 Calculator - singleton s cached ETV tabuľkou
class WN8Calculator {
public:
    struct ETVData {
        float frag, dmg, spot, def, win;
    };

    static WN8Calculator& getInstance() {
        static WN8Calculator instance;
        return instance;
    }

    float calculate(const wn_data& player_data, const vehicle_battles& tank_data) const {
        double expDMG = 0.0, expSpot = 0.0, expFrag = 0.0, expDef = 0.0, expWin = 0.0;

        for (const auto& [tank_id, battles] : tank_data) {
            auto it = etv_table.find(tank_id);
            if (it == etv_table.end()) continue;

            const ETVData& etv = it->second;
            double b = static_cast<double>(battles);

            expDMG  += etv.dmg  * b;
            expSpot += etv.spot * b;
            expFrag += etv.frag * b;
            expDef  += etv.def  * b;
            expWin  += 0.01 * etv.win * b;
        }

        if (expDMG == 0.0) return 0.0f;

        double rDMG  = player_data.dmg   / expDMG;
        double rSpot = player_data.spot  / expSpot;
        double rFrag = player_data.frags / expFrag;
        double rDef  = player_data.dcp   / expDef;
        double rWin  = player_data.wins  / expWin;

        double rDMGc  = max(0.0, (rDMG  - 0.22) / 0.78);
        double rSpotc = max(0.0, min(rDMGc + 0.1, (rSpot - 0.38) / 0.62));
        double rFragc = max(0.0, min(rDMGc + 0.2, (rFrag - 0.12) / 0.88));
        double rDefc  = max(0.0, min(rDMGc + 0.1, (rDef  - 0.10) / 0.90));
        double rWinc  = max(0.0, (rWin  - 0.71) / 0.29);

        return static_cast<float>(
            980.0 * rDMGc +
            210.0 * rDMGc * rFragc +
            155.0 * rFragc * rSpotc +
            75.0  * rDefc * rFragc +
            145.0 * min(1.8, rWinc)
        );
    }

private:
    unordered_map<int, ETVData> etv_table;

    WN8Calculator() {
        load_etv_table();
    }

    void load_etv_table() {
        Pgsql pgsql;
        PGconn* conn = pgsql.Get();
        if (!conn) {
            cerr << "Failed to connect to database for ETV table" << endl;
            return;
        }

        PGresult* result = PQexec(conn, "SELECT * FROM expected_tank_value");
        if (PQresultStatus(result) != PGRES_TUPLES_OK) {
            cerr << "Error loading ETV table: " << PQresultErrorMessage(result) << endl;
            PQclear(result);
            return;
        }

        int ntuples = PQntuples(result);
        etv_table.reserve(ntuples);

        for (int i = 0; i < ntuples; i++) {
            int tank_id = stoi(PQgetvalue(result, i, 0));
            ETVData data{
                stof(PQgetvalue(result, i, 1)),  // frag
                stof(PQgetvalue(result, i, 2)),  // dmg
                stof(PQgetvalue(result, i, 3)),  // spot
                stof(PQgetvalue(result, i, 4)),  // def
                stof(PQgetvalue(result, i, 5))   // win
            };
            etv_table[tank_id] = data;
        }

        PQclear(result);
        // Destruktor Pgsql automaticky zavolá PQfinish

        cout << "Loaded " << etv_table.size() << " entries from ETV table" << endl;
    }

    WN8Calculator(const WN8Calculator&) = delete;
    WN8Calculator& operator=(const WN8Calculator&) = delete;
};

// Načítanie account_id z databázy
vector<int> LoadAccountIds() {
    Pgsql pgsql;
    PGconn* conn = pgsql.Get();
    if (!conn) {
        cerr << "Failed to connect to database" << endl;
        return {};
    }

    PGresult* result = PQexec(conn, MASTER_QUERY);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        cerr << "Error in LoadAccountIds: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return {};
    }

    int ntuples = PQntuples(result);
    vector<int> ids;
    ids.reserve(ntuples);

    for (int i = 0; i < ntuples; i++) {
        ids.push_back(stoi(PQgetvalue(result, i, 0)));
    }

    PQclear(result);
    // Destruktor Pgsql automaticky zavolá PQfinish

    return ids;
}

// Vytvorenie ID stringu pre API
string CreateIdString(const vector<int>& ids) {
    if (ids.empty()) return "";

    string result;
    result.reserve(ids.size() * 10); // Priemerná dĺžka čísla

    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) result += ',';
        result += to_string(ids[i]);
    }

    return result;
}

// Spočítanie WN8 pre batch hráčov
wn8value CalculateWN8Batch(const PlayersStats& player_stats, const Data_vehicle& vehicles) {
    wn8value results;
    results.reserve(player_stats.size());

    const WN8Calculator& calculator = WN8Calculator::getInstance();

    for (const auto& [account_id, stats] : player_stats) {
        auto vehicle_it = vehicles.find(account_id);
        if (vehicle_it == vehicles.end()) continue;

        float wn8 = calculator.calculate(stats, vehicle_it->second);
        results[account_id] = wn8;
    }

    return results;
}

// Uloženie WN8 hodnôt do databázy
void SaveWN8Values(const wn8value& wn8_values) {
    if (wn8_values.empty()) return;

    Pgsql pgsql;
    PGconn* conn = pgsql.Get();
    if (!conn) {
        cerr << "Failed to connect to database for saving" << endl;
        return;
    }

    // DELETE query
    string q_delete = "DELETE FROM wn8player WHERE account_id IN (";
    q_delete.reserve(wn8_values.size() * 10 + 50);

    bool first = true;
    for (const auto& [account_id, _] : wn8_values) {
        if (!first) q_delete += ',';
        q_delete += to_string(account_id);
        first = false;
    }
    q_delete += ')';

    PGresult* result = PQexec(conn, q_delete.c_str());
    PQclear(result);

    // INSERT query - použitie batch insertu
    string q_insert = "INSERT INTO wn8player (account_id, wn8) VALUES ";
    q_insert.reserve(wn8_values.size() * 25 + 50);

    first = true;
    for (const auto& [account_id, wn8] : wn8_values) {
        if (!first) q_insert += ',';
        q_insert += '(' + to_string(account_id) + ',' + to_string(wn8) + ')';
        first = false;
    }

    result = PQexec(conn, q_insert.c_str());
    PQclear(result);
    // Destruktor Pgsql automaticky zavolá PQfinish
}

// Worker thread funkcia
void WorkerThread(ThreadSafeQueue& queue, WN8Buffer& wn8_buffer, atomic<int>& processed) {
    GetPlayersStats player_stats_api;
    GetPlayersVehicles vehicles_api;

    while (true) {
        vector<int> batch;
        if (!queue.get_batch(batch, BATCH_SIZE)) {
            break; // Koniec práce
        }

        string id_string = CreateIdString(batch);

        PlayersStats player_stats;
        Data_vehicle vehicles;

        // Stiahnutie dát
        player_stats_api.Gets(id_string, &player_stats);
        vehicles_api.Gets(id_string, &vehicles);

        // Výpočet WN8
        wn8value wn8_results = CalculateWN8Batch(player_stats, vehicles);

        // Uloženie do bufferu
        wn8_buffer.insert(wn8_results);

        processed.fetch_add(batch.size(), memory_order_relaxed);

        // Uloženie do DB ak je buffer dosť veľký
        wn8value to_save = wn8_buffer.extract_if_threshold(SAVE_THRESHOLD);
        if (!to_save.empty()) {
            SaveWN8Values(to_save);
            cout << "Saved " << to_save.size() << " WN8 values (processed: "
                 << processed.load() << ")" << endl;
        }
    }
}

int main() {
    timestamp_t t_start = get_timestamp();
    time_t start_time = time(nullptr);
    cout << "\nProgram started: " << ctime(&start_time) << endl;

    // Načítanie account_id
    vector<int> account_ids = LoadAccountIds();
    cout << "Total players to process: " << account_ids.size() << endl;

    if (account_ids.empty()) {
        cout << "No players to process" << endl;
        return 0;
    }

    // Inicializácia WN8 kalkulátora (načíta ETV tabuľku)
    WN8Calculator::getInstance();

    // Thread-safe štruktúry
    ThreadSafeQueue queue;
    queue.push_bulk(move(account_ids));

    WN8Buffer wn8_buffer;
    atomic<int> processed(0);

    // Spustenie worker threads
    vector<thread> workers;
    workers.reserve(NUM_THREADS);

    cout << "Starting " << NUM_THREADS << " worker threads..." << endl;

    for (size_t i = 0; i < NUM_THREADS; i++) {
        workers.emplace_back(WorkerThread, ref(queue), ref(wn8_buffer), ref(processed));
    }

    // Čakanie na dokončenie
    for (auto& worker : workers) {
        worker.join();
    }

    // Uloženie zostávajúcich hodnôt
    wn8value remaining = wn8_buffer.extract_all();
    if (!remaining.empty()) {
        SaveWN8Values(remaining);
        cout << "Saved remaining " << remaining.size() << " WN8 values" << endl;
    }

    // Výpis času
    timestamp_t t_end = get_timestamp();
    double seconds = (t_end - t_start) / 1000000.0;
    time_t end_time = time(nullptr);

    cout << "\nTotal processed: " << processed.load() << " players" << endl;
    cout << "Time elapsed: " << seconds << " seconds" << endl;
    cout << "Program finished: " << ctime(&end_time) << endl;

    return 0;
}
