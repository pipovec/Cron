/*
 * PlayersInfo2 - Spracovanie informacii o hracoch
 * Hlada client language, global rating.
 *
 * Author: Boris Fekar
 * Refactor: 2026-02-28
 */

#include <algorithm>
#include <chrono>
#include <exception>
#include <ctime>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include <nlohmann/json.hpp>

namespace {

constexpr int kBatchSize = 100;
constexpr int kMaxRetries = 3;
constexpr int kBaseRetryDelaySeconds = 2;
const std::string kField = "&fields=account_id,client_language,global_rating";
const std::string kMethod = "/account/info/";

struct PlayerData {
    int account_id = 0;
    int global_rating = 0;
    std::string client_language;
};

struct PgResultDeleter {
    void operator()(PGresult* result) const {
        if (result != nullptr) {
            PQclear(result);
        }
    }
};

using PgResultPtr = std::unique_ptr<PGresult, PgResultDeleter>;

class PlayersInfoProcessor {
public:
    bool Init();
    std::string Run();

private:
    bool PrepareStatements();
    bool LoadAccountIds();
    std::string BuildPost(size_t offset) const;
    std::string FetchJson(const std::string& post);
    void ParseJson(const std::string& json_data);
    bool CheckAndUpsert();
    void VacuumAnalyze();

    std::unique_ptr<Pgsql> db_;
    PGconn* conn_ = nullptr;
    SendCurl curl_;

    std::vector<int> account_ids_;
    std::unordered_map<int, PlayerData> players_;
    size_t max_players_ = 0;

    int insert_counter_ = 0;
    int update_counter_ = 0;
};

bool PlayersInfoProcessor::Init()
{
    db_ = std::make_unique<Pgsql>();
    conn_ = db_->Get();

    if (conn_ == nullptr || PQstatus(conn_) != CONNECTION_OK) {
        std::cerr << "Chyba: Nepodarilo sa pripojit k databaze!" << std::endl;
        return false;
    }

    if (!PrepareStatements()) {
        return false;
    }

    if (const char* limit_env = std::getenv("PLAYERSINFO2_LIMIT")) {
        try {
            max_players_ = static_cast<size_t>(std::stoul(limit_env));
        }
        catch (...) {
            std::cerr << "Neplatna hodnota PLAYERSINFO2_LIMIT, ignorujem." << std::endl;
            max_players_ = 0;
        }
    }

    if (!LoadAccountIds()) {
        return false;
    }

    return true;
}

bool PlayersInfoProcessor::PrepareStatements()
{
    const char* query =
        "INSERT INTO players_info (account_id,global_rating,client_language) "
        "VALUES ($1,$2,$3) "
        "ON CONFLICT (account_id) DO UPDATE "
        "SET global_rating = EXCLUDED.global_rating, "
        "    client_language = EXCLUDED.client_language "
        "WHERE players_info.global_rating IS DISTINCT FROM EXCLUDED.global_rating "
        "   OR players_info.client_language IS DISTINCT FROM EXCLUDED.client_language "
        "RETURNING (xmax = 0) AS inserted";

    PgResultPtr result(db_->Prepare("players_info_upsert", query, 3));
    if (result == nullptr || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << "Prepared statement players_info_upsert je chybny: "
                  << PQerrorMessage(conn_) << std::endl;
        return false;
    }

    return true;
}

bool PlayersInfoProcessor::LoadAccountIds()
{
    std::string sql = "SELECT account_id FROM players_all p "
                       "WHERE NOT EXISTS (SELECT 1 FROM players_dead pd WHERE pd.account_id = p.account_id) "
                       "ORDER BY account_id DESC";
    if (max_players_ > 0) {
        sql += " LIMIT " + std::to_string(max_players_);
    }

    PgResultPtr result(db_->Query(sql.c_str()));
    if (result == nullptr || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba pri nacitani account_id z players_all: "
                  << PQerrorMessage(conn_) << std::endl;
        return false;
    }

    int ntuples = PQntuples(result.get());
    account_ids_.reserve(static_cast<size_t>(ntuples));

    for (int i = 0; i < ntuples; ++i) {
        account_ids_.push_back(std::stoi(PQgetvalue(result.get(), i, 0)));
    }

    std::cout << "Celkom hracov spracovanie: " << account_ids_.size() << std::endl;
    return true;
}

std::string PlayersInfoProcessor::BuildPost(size_t offset) const
{
    if (offset >= account_ids_.size()) {
        return {};
    }

    size_t end = std::min(offset + static_cast<size_t>(kBatchSize), account_ids_.size());
    std::string ids;
    ids.reserve((end - offset) * 12);

    for (size_t i = offset; i < end; ++i) {
        if (!ids.empty()) {
            ids += ",";
        }
        ids += std::to_string(account_ids_[i]);
    }

    return kField + "&account_id=" + ids;
}

std::string PlayersInfoProcessor::FetchJson(const std::string& post)
{
    if (post.empty()) {
        return {};
    }

    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        try {
            std::string data = curl_.SendWOT(kMethod, post);
            if (!data.empty()) {
                return data;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Chyba pri SendWOT (pokus " << attempt << "): "
                      << e.what() << std::endl;
        }
        catch (const std::string& e) {
            std::cerr << "Chyba pri SendWOT (pokus " << attempt << "): "
                      << e << std::endl;
        }
        catch (...) {
            std::cerr << "Chyba pri SendWOT (pokus " << attempt
                      << "): neznamy error" << std::endl;
        }

        if (attempt < kMaxRetries) {
            int delay = kBaseRetryDelaySeconds * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }
    }

    return {};
}

void PlayersInfoProcessor::ParseJson(const std::string& json_data)
{
    if (json_data.empty()) {
        return;
    }

    using json = nlohmann::json;

    json js;
    try {
        js = json::parse(json_data);
    }
    catch (const json::parse_error& e) {
        std::cerr << "Parser error: " << e.what() << std::endl;
        return;
    }

    if (!js.contains("status") || js["status"].get<std::string>() != "ok") {
        return;
    }

    if (!js.contains("data") || js["data"].is_null()) {
        return;
    }

    json udaje = js["data"];

    for (auto& item : udaje.items()) {
        if (item.value().is_null()) {
            continue;
        }

        json j = item.value();
        if (j.is_null()) {
            continue;
        }

        if (!j.contains("account_id") || !j.contains("global_rating") || !j.contains("client_language")) {
            continue;
        }

        if (j["account_id"].is_null() || j["global_rating"].is_null() || j["client_language"].is_null()) {
            continue;
        }

        PlayerData data;
        data.account_id = j["account_id"].get<int>();
        data.global_rating = j["global_rating"].get<int>();
        data.client_language = j["client_language"].get<std::string>();

        players_[data.account_id] = std::move(data);
    }
}

bool PlayersInfoProcessor::CheckAndUpsert()
{
    if (players_.empty()) {
        return true;
    }

    for (const auto& entry : players_) {
        const int account_id = entry.first;
        const PlayerData& data = entry.second;

        const std::string account_id_str = std::to_string(account_id);
        const std::string global_rating_str = std::to_string(data.global_rating);
        const std::string client_language_str = data.client_language;

        const char* paramValues[3] = {
            account_id_str.c_str(),
            global_rating_str.c_str(),
            client_language_str.c_str()
        };

        PgResultPtr result(db_->ExecutePrepared("players_info_upsert", 3, paramValues));
        if (result == nullptr || PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
            std::cerr << "Upsert statement players_info je chybny: "
                      << PQerrorMessage(conn_) << std::endl;
            return false;
        }

        int rows = PQntuples(result.get());
        if (rows == 1) {
            const char* inserted = PQgetvalue(result.get(), 0, 0);
            if (inserted != nullptr && inserted[0] == 't') {
                ++insert_counter_;
            } else {
                ++update_counter_;
            }
        }
    }

    return true;
}

void PlayersInfoProcessor::VacuumAnalyze()
{
    const char* sql = "VACUUM ANALYZE players_info";
    PgResultPtr result(db_->Query(sql));

    if (result == nullptr || PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        std::cerr << "VACUUM ANALYZE players_info bolo chybne: "
                  << PQerrorMessage(conn_) << std::endl;
        return;
    }

    std::cout << "Prebehlo Vacuum Analyze players_info" << std::endl;
}

std::string PlayersInfoProcessor::Run()
{
    auto start = std::chrono::high_resolution_clock::now();
    int kolo = 0;

    std::time_t start_time = std::time(nullptr);
    std::cout << "Program zacal pracovat o: " << std::ctime(&start_time) << std::endl;

    for (size_t offset = 0; offset < account_ids_.size(); offset += kBatchSize) {
        ++kolo;
        std::string post = BuildPost(offset);
        std::string json_data = FetchJson(post);
        ParseJson(json_data);

        if (!db_->BeginTransaction()) {
            std::cerr << "Chyba: nepodarilo sa spustit transakciu." << std::endl;
            break;
        }

        bool ok = CheckAndUpsert();
        if (ok) {
            if (!db_->CommitTransaction()) {
                std::cerr << "Chyba: nepodarilo sa commitnut transakciu." << std::endl;
                db_->RollbackTransaction();
            }
        } else {
            std::cerr << "Chyba: upsert zlyhal, rollbackujem transakciu." << std::endl;
            db_->RollbackTransaction();
        }

        players_.clear();
    }

    VacuumAnalyze();

    std::cout << "Pocet spracovanych updatov: " << update_counter_ << std::endl;
    std::cout << "Pocet spracovanych insertov: " << insert_counter_ << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::time_t end_time = std::time(nullptr);
    std::cout << std::endl << "Program skoncil o: " << std::ctime(&end_time) << std::endl;
    std::cout << "Celkovy cas: " << elapsed_seconds.count() << " s" << std::endl << std::endl;

    std::ostringstream info;
    info << std::fixed << std::setprecision(3);
    info << "STATISTIKA SPRACOVANIA "
         << "Typ=PlayersInfo2"
         << " Kola=" << kolo
         << " Cas=" << elapsed_seconds.count() << "s"
         << " Klany=0"
         << " Hraci=" << account_ids_.size()
         << " Prichody=0"
         << " Odchody=0"
         << " NoveID=0"
         << " Nicky=0"
         << " Ukoncene=0"
         << " Updaty=" << update_counter_
         << " Inserty=" << insert_counter_
         << " Spracovane=" << (update_counter_ + insert_counter_)
         << " MissingStats=0"
         << " MissingVehicles=0"
         << " Neaktivne=0";
    return info.str();
}

} // namespace

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersInfo2");

    PlayersInfoProcessor processor;
    if (!processor.Init()) {
        cron_log.SetInfo("ERROR: init failed");
        return 1;
    }

    cron_log.SetInfo(processor.Run());
    return 0;
}
