#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <libpq-fe.h>

#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include "./lib/GetPlayersStats.h"
#include "./lib/GetPlayersVehicles.h"
#include "./lib/Wn8Formula.h"

namespace {

constexpr const char* kMasterQuery =
    "SELECT account_id FROM players_all p "
    "WHERE NOT EXISTS (SELECT 1 FROM players_dead pd WHERE pd.account_id = p.account_id)";
constexpr int kDefaultBatchSize = 100;
constexpr int kSaveThreshold = 5000;
constexpr size_t kDefaultNumThreads = 6;

using Wn8Values = std::unordered_map<int, float>;

struct PgResultDeleter {
    void operator()(PGresult* result) const {
        if (result != nullptr) {
            PQclear(result);
        }
    }
};

using PgResultPtr = std::unique_ptr<PGresult, PgResultDeleter>;

class ThreadSafeQueue {
public:
    void PushBulk(std::vector<int>&& ids) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_ = std::move(ids);
        read_pos_ = 0;
    }

    bool GetBatch(std::vector<int>& batch, size_t batch_size) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (read_pos_ >= data_.size()) {
            return false;
        }

        size_t end = std::min(read_pos_ + batch_size, data_.size());
        batch.clear();
        batch.reserve(end - read_pos_);
        batch.insert(batch.end(), data_.begin() + read_pos_, data_.begin() + end);
        read_pos_ = end;
        return true;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.size() - read_pos_;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return read_pos_ >= data_.size();
    }

private:
    std::vector<int> data_;
    mutable std::mutex mtx_;
    size_t read_pos_ = 0;
};

class WN8Buffer {
public:
    void Insert(const Wn8Values& values) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.insert(values.begin(), values.end());
    }

    Wn8Values ExtractAll() {
        std::lock_guard<std::mutex> lock(mtx_);
        Wn8Values result = std::move(data_);
        data_.clear();
        return result;
    }

    Wn8Values ExtractIfThreshold(size_t threshold) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.size() >= threshold) {
            Wn8Values result = std::move(data_);
            data_.clear();
            return result;
        }
        return {};
    }

private:
    Wn8Values data_;
    mutable std::mutex mtx_;
};

class WN8Calculator {
public:
    struct ETVData {
        float frag;
        float dmg;
        float spot;
        float def;
        float win;
    };

    static WN8Calculator& GetInstance() {
        static WN8Calculator instance;
        return instance;
    }

    float Calculate(const wn_data& player_data, const vehicle_battles& tank_data) const {
        double expDMG = 0.0, expSpot = 0.0, expFrag = 0.0, expDef = 0.0, expWin = 0.0;

        for (const auto& [tank_id, battles] : tank_data) {
            auto it = etv_table_.find(tank_id);
            if (it == etv_table_.end()) {
                continue;
            }

            const ETVData& etv = it->second;
            double b = static_cast<double>(battles);

            expDMG  += etv.dmg  * b;
            expSpot += etv.spot * b;
            expFrag += etv.frag * b;
            expDef  += etv.def  * b;
            expWin  += 0.01 * etv.win * b;
        }

        if (expDMG == 0.0) {
            return 0.0f;
        }

        return Wn8::Compute(
            {player_data.dmg, player_data.spot, player_data.frags, player_data.dcp, player_data.wins},
            {expDMG, expSpot, expFrag, expDef, expWin});
    }

private:
    std::unordered_map<int, ETVData> etv_table_;

    WN8Calculator() {
        LoadEtvTable();
    }

    void LoadEtvTable() {
        Pgsql pgsql;
        PGconn* conn = pgsql.Get();
        if (!conn) {
            std::cerr << "Failed to connect to database for ETV table" << std::endl;
            return;
        }

        PgResultPtr result(PQexec(conn, "SELECT * FROM expected_tank_value"));
        if (result == nullptr || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
            std::cerr << "Error loading ETV table: " << PQresultErrorMessage(result.get()) << std::endl;
            return;
        }

        int ntuples = PQntuples(result.get());
        etv_table_.reserve(static_cast<size_t>(ntuples));

        for (int i = 0; i < ntuples; i++) {
            int tank_id = std::stoi(PQgetvalue(result.get(), i, 0));
            ETVData data{
                std::stof(PQgetvalue(result.get(), i, 1)),
                std::stof(PQgetvalue(result.get(), i, 2)),
                std::stof(PQgetvalue(result.get(), i, 3)),
                std::stof(PQgetvalue(result.get(), i, 4)),
                std::stof(PQgetvalue(result.get(), i, 5))
            };
            etv_table_[tank_id] = data;
        }

        std::cout << "Loaded " << etv_table_.size() << " entries from ETV table" << std::endl;
    }

    WN8Calculator(const WN8Calculator&) = delete;
    WN8Calculator& operator=(const WN8Calculator&) = delete;
};

std::vector<int> LoadAccountIds(size_t limit) {
    Pgsql pgsql;
    PGconn* conn = pgsql.Get();
    if (!conn) {
        std::cerr << "Failed to connect to database" << std::endl;
        return {};
    }

    std::string query = kMasterQuery;
    if (limit > 0) {
        query += " LIMIT " + std::to_string(limit);
    }

    PgResultPtr result(PQexec(conn, query.c_str()));
    if (result == nullptr || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        std::cerr << "Error in LoadAccountIds: " << PQresultErrorMessage(result.get()) << std::endl;
        return {};
    }

    int ntuples = PQntuples(result.get());
    std::vector<int> ids;
    ids.reserve(static_cast<size_t>(ntuples));

    for (int i = 0; i < ntuples; i++) {
        ids.push_back(std::stoi(PQgetvalue(result.get(), i, 0)));
    }

    return ids;
}

std::string CreateIdString(const std::vector<int>& ids) {
    if (ids.empty()) {
        return "";
    }

    std::string result;
    result.reserve(ids.size() * 10);

    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) {
            result += ',';
        }
        result += std::to_string(ids[i]);
    }

    return result;
}

Wn8Values CalculateWN8Batch(const PlayersStats& player_stats, const Data_vehicle& vehicles, int& missing_vehicles) {
    Wn8Values results;
    results.reserve(player_stats.size());

    const WN8Calculator& calculator = WN8Calculator::GetInstance();

    for (const auto& [account_id, stats] : player_stats) {
        auto vehicle_it = vehicles.find(account_id);
        if (vehicle_it == vehicles.end()) {
            ++missing_vehicles;
            continue;
        }

        float wn8 = calculator.Calculate(stats, vehicle_it->second);
        results[account_id] = wn8;
    }

    return results;
}

void SaveWN8Values(const Wn8Values& wn8_values) {
    if (wn8_values.empty()) {
        return;
    }

    Pgsql pgsql;
    PGconn* conn = pgsql.Get();
    if (!conn) {
        std::cerr << "Failed to connect to database for saving" << std::endl;
        return;
    }

    if (!pgsql.BeginTransaction()) {
        std::cerr << "Failed to begin transaction for saving" << std::endl;
        return;
    }

    std::string q_delete = "DELETE FROM wn8player WHERE account_id IN (";
    q_delete.reserve(wn8_values.size() * 10 + 50);

    bool first = true;
    for (const auto& [account_id, _] : wn8_values) {
        if (!first) {
            q_delete += ',';
        }
        q_delete += std::to_string(account_id);
        first = false;
    }
    q_delete += ')';

    PgResultPtr result(PQexec(conn, q_delete.c_str()));
    if (result == nullptr || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << "Delete failed: " << PQresultErrorMessage(result.get()) << std::endl;
        pgsql.RollbackTransaction();
        return;
    }

    std::string q_insert = "INSERT INTO wn8player (account_id, wn8) VALUES ";
    q_insert.reserve(wn8_values.size() * 25 + 50);

    first = true;
    for (const auto& [account_id, wn8] : wn8_values) {
        if (!first) {
            q_insert += ',';
        }
        q_insert += '(' + std::to_string(account_id) + ',' + std::to_string(wn8) + ')';
        first = false;
    }

    result.reset(PQexec(conn, q_insert.c_str()));
    if (result == nullptr || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << "Insert failed: " << PQresultErrorMessage(result.get()) << std::endl;
        pgsql.RollbackTransaction();
        return;
    }

    if (!pgsql.CommitTransaction()) {
        std::cerr << "Commit failed for saving" << std::endl;
        pgsql.RollbackTransaction();
    }
}

void WorkerThread(ThreadSafeQueue& queue, WN8Buffer& wn8_buffer, std::atomic<int>& processed,
                  std::atomic<int>& missing_stats, std::atomic<int>& missing_vehicles, size_t batch_size) {
    GetPlayersStats player_stats_api;
    GetPlayersVehicles vehicles_api;

    while (true) {
        std::vector<int> batch;
        if (!queue.GetBatch(batch, batch_size)) {
            break;
        }

        std::string id_string = CreateIdString(batch);
        PlayersStats player_stats;
        Data_vehicle vehicles;

        player_stats_api.Gets(id_string, &player_stats);
        vehicles_api.Gets(id_string, &vehicles);

        int local_skipped = 0;
        if (player_stats.size() < batch.size()) {
            int diff = static_cast<int>(batch.size() - player_stats.size());
            missing_stats.fetch_add(diff, std::memory_order_relaxed);
        }

        Wn8Values wn8_results = CalculateWN8Batch(player_stats, vehicles, local_skipped);
        wn8_buffer.Insert(wn8_results);

        processed.fetch_add(static_cast<int>(batch.size()), std::memory_order_relaxed);
        missing_vehicles.fetch_add(local_skipped, std::memory_order_relaxed);

        Wn8Values to_save = wn8_buffer.ExtractIfThreshold(kSaveThreshold);
        if (!to_save.empty()) {
            SaveWN8Values(to_save);
            std::cout << "Saved " << to_save.size() << " WN8 values (processed: "
                      << processed.load() << ")" << std::endl;
        }
    }
}

class WN8Runner {
public:
    std::string Run() {
        const size_t limit = ReadLimit();
        const size_t num_threads = ReadNumThreads();
        const size_t batch_size = ReadBatchSize();

        auto start = std::chrono::high_resolution_clock::now();
        std::time_t start_time = std::time(nullptr);
        std::cout << "\nProgram started: " << std::ctime(&start_time) << std::endl;

        std::vector<int> account_ids = LoadAccountIds(limit);
        std::cout << "Total players to process: " << account_ids.size() << std::endl;

        if (account_ids.empty()) {
            std::cout << "No players to process" << std::endl;
            return "STATISTIKA SPRACOVANIA Typ=PlayersWN8v2 Kola=0 Cas=0.000s Klany=0 Hraci=0 Prichody=0 Odchody=0 NoveID=0 Nicky=0 Ukoncene=0 Updaty=0 Inserty=0 Spracovane=0 MissingStats=0 MissingVehicles=0 Neaktivne=0";
        }

        WN8Calculator::GetInstance();

        ThreadSafeQueue queue;
        queue.PushBulk(std::move(account_ids));

        WN8Buffer wn8_buffer;
        std::atomic<int> processed(0);
        std::atomic<int> missing_stats(0);
        std::atomic<int> missing_vehicles(0);

        std::vector<std::thread> workers;
        workers.reserve(num_threads);

        std::cout << "Starting " << num_threads << " worker threads..." << std::endl;

        for (size_t i = 0; i < num_threads; i++) {
            workers.emplace_back(WorkerThread, std::ref(queue), std::ref(wn8_buffer), std::ref(processed),
                                 std::ref(missing_stats), std::ref(missing_vehicles), batch_size);
        }

        for (auto& worker : workers) {
            worker.join();
        }

        Wn8Values remaining = wn8_buffer.ExtractAll();
        if (!remaining.empty()) {
            SaveWN8Values(remaining);
            std::cout << "Saved remaining " << remaining.size() << " WN8 values" << std::endl;
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        std::time_t end_time = std::time(nullptr);

        std::cout << "\nTotal processed: " << processed.load() << " players" << std::endl;
        std::cout << "Missing stats: " << missing_stats.load() << std::endl;
        std::cout << "Missing vehicles: " << missing_vehicles.load() << std::endl;
        std::cout << "Time elapsed: " << elapsed.count() << " seconds" << std::endl;
        std::cout << "Program finished: " << std::ctime(&end_time) << std::endl;

        std::ostringstream info;
        info << std::fixed << std::setprecision(3);
        info << "STATISTIKA SPRACOVANIA "
             << "Typ=PlayersWN8v2"
             << " Kola=0"
             << " Cas=" << elapsed.count() << "s"
             << " Klany=0"
             << " Hraci=" << processed.load()
             << " Prichody=0"
             << " Odchody=0"
             << " NoveID=0"
             << " Nicky=0"
             << " Ukoncene=0"
             << " Updaty=0"
             << " Inserty=0"
             << " Spracovane=" << processed.load()
             << " MissingStats=" << missing_stats.load()
             << " MissingVehicles=" << missing_vehicles.load()
             << " Neaktivne=0";
        return info.str();
    }

private:
    size_t ReadNumThreads() const {
        if (const char* value = std::getenv("WN8_THREADS")) {
            try {
                size_t parsed = static_cast<size_t>(std::stoul(value));
                return parsed > 0 ? parsed : kDefaultNumThreads;
            }
            catch (...) {
                std::cerr << "Neplatna hodnota WN8_THREADS, pouzivam default." << std::endl;
            }
        }
        return kDefaultNumThreads;
    }

    size_t ReadBatchSize() const {
        if (const char* value = std::getenv("WN8_BATCH_SIZE")) {
            try {
                size_t parsed = static_cast<size_t>(std::stoul(value));
                return parsed > 0 ? parsed : static_cast<size_t>(kDefaultBatchSize);
            }
            catch (...) {
                std::cerr << "Neplatna hodnota WN8_BATCH_SIZE, pouzivam default." << std::endl;
            }
        }
        return static_cast<size_t>(kDefaultBatchSize);
    }

    size_t ReadLimit() const {
        if (const char* value = std::getenv("WN8_LIMIT")) {
            try {
                return static_cast<size_t>(std::stoul(value));
            }
            catch (...) {
                std::cerr << "Neplatna hodnota WN8_LIMIT, ignorujem." << std::endl;
            }
        }
        return 0;
    }
};

} // namespace

int main() {
    CronLog::ScopedRun cron_log("cplusplus:PlayersWN8v2");
    try {
        WN8Runner runner;
        cron_log.SetInfo(runner.Run());
        return 0;
    }
    catch (const std::exception& e) {
        cron_log.SetInfo(std::string("ERROR: ") + e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        cron_log.SetInfo("ERROR: unknown exception");
        std::cerr << "Fatal error: unknown exception" << std::endl;
        return 1;
    }
}
