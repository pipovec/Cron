/*
 * PlayersVehicleStat - stav hracov po tankoch do player_vehicle_stat
 *
 * C++ nahrada PHP cron:player-vehicle-stat (admin). Pre kazdu davku 100 uctov stiahne
 * /account/tanks/ (battles, wins, mark_of_mastery) a jednym upsertom zapise len zmenene riadky.
 *
 * Zoznam uctov sa nacita raz do pamate, davky si vlakna berazu atomickym indexom. Kazde vlakno
 * ma vlastne DB spojenie aj vlastny curl (keep-alive).
 *
 * Rezim (argument "active" | "full", alebo VSTAT_MODE):
 *   active  hraci, ktori maju riadok v players_stat_all_history za VSTAT_DATE (default vcera)
 *   full    vsetci hraci z players_stat_all
 *
 * Premenne prostredia:
 *   VSTAT_MODE     active | full (default active)
 *   VSTAT_DATE     den historie pre rezim active, YYYY-MM-DD (default vcera)
 *   VSTAT_THREADS  pocet vlakien, 1 az 16 (default 8)
 *   VSTAT_LIMIT    spracovat najviac N hracov (test), 0 = vsetkych
 *   VSTAT_DRY_RUN  1 = stiahne a spocita, nic nezapisuje
 *
 * Navratovy kod: 0 ok, 1 chyba DB, 2 niektore davky zlyhali.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "./lib/CronLog.h"
#include "./lib/Pgsql.h"
#include "./lib/PlayerVehicleStat.h"
#include "./lib/SendCurl.h"

static const size_t kBatch = 100;

static long long EnvLongLong(const char *name, long long fallback)
{
    const char *v = std::getenv(name);
    if (v == nullptr || *v == '\0')
        return fallback;
    try
    {
        return std::stoll(v);
    }
    catch (...)
    {
        std::cerr << "Neplatna hodnota " << name << ", pouzivam " << fallback << std::endl;
        return fallback;
    }
}

struct Shared
{
    std::vector<int> ids;
    std::atomic<size_t> next{0};
    bool dry_run = false;

    std::atomic<long long> players{0}, inserted{0}, updated{0}, dead{0}, batches{0}, failed{0};
    std::atomic<bool> db_error{false};

    std::mutex print_mtx;
    std::chrono::steady_clock::time_point t0;
};

static bool LoadIds(PGconn *conn, bool full, const std::string &date, long long limit, std::vector<int> *ids)
{
    std::string query = full ? "SELECT account_id FROM players_stat_all ORDER BY account_id"
                             : "SELECT account_id FROM players_stat_all_history WHERE date = $1::date "
                               "ORDER BY account_id";
    if (limit > 0)
        query += " LIMIT " + std::to_string(limit);

    const char *params[1] = {date.c_str()};
    PGresult *result = PQexecParams(conn, query.c_str(), full ? 0 : 1, nullptr, full ? nullptr : params, nullptr,
                                    nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba vyberu uctov: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    ids->reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
        ids->push_back(std::atoi(PQgetvalue(result, i, 0)));
    PQclear(result);
    return true;
}

static void Worker(Shared &sh, Pgsql &db)
{
    try
    {
        SendCurl send;
        PGconn *conn = db.Get();
        if (conn == nullptr)
        {
            std::cerr << "Vlakno: bez spojenia s databazou" << std::endl;
            sh.db_error = true;
            return;
        }

        while (true)
        {
            size_t begin = sh.next.fetch_add(kBatch);
            if (begin >= sh.ids.size())
                return;
            size_t end = std::min(begin + kBatch, sh.ids.size());
            std::vector<int> ids(sh.ids.begin() + static_cast<long>(begin), sh.ids.begin() + static_cast<long>(end));

            PlayerVehicleStat::Batch batch;
            bool ok = PlayerVehicleStat::Fetch(send, ids, &batch);
            if (ok)
            {
                sh.dead += batch.dead_accounts;
                if (!sh.dry_run)
                {
                    PlayerVehicleStat::UpsertCount c;
                    ok = PlayerVehicleStat::Upsert(conn, batch.rows, &c);
                    if (ok)
                    {
                        sh.inserted += c.inserted;
                        sh.updated += c.updated;
                    }
                    else if (PQstatus(conn) != CONNECTION_OK)
                    {
                        sh.db_error = true;
                        PQreset(conn); // dalsie davky skusime na obnovenom spojeni
                    }
                }
            }

            if (!ok)
            {
                sh.failed++;
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "ZLYHALA DAVKA od account_id=" << ids.front() << " (" << ids.size() << " uctov)"
                          << std::endl;
            }

            sh.players += static_cast<long long>(ids.size());
            long long b = ++sh.batches;
            if (b % 200 == 0)
            {
                double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "hracov " << sh.players.load() << "/" << sh.ids.size() << ", vlozenych "
                          << sh.inserted.load() << ", aktualizovanych " << sh.updated.load() << ", "
                          << static_cast<long long>(secs) << " s" << std::endl;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Vlakno skoncilo vynimkou: " << e.what() << std::endl;
        sh.db_error = true;
    }
    catch (...)
    {
        std::cerr << "Vlakno skoncilo neznamou vynimkou" << std::endl;
        sh.db_error = true;
    }
}

int main(int argc, char **argv)
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersVehicleStat");

    std::string mode = std::getenv("VSTAT_MODE") ? std::getenv("VSTAT_MODE") : "active";
    if (argc > 1)
        mode = argv[1];
    if (mode != "active" && mode != "full")
    {
        std::cerr << "Pouzitie: PlayersVehicleStat [active|full]" << std::endl;
        return 1;
    }
    const bool full = mode == "full";

    const long long threads = std::max<long long>(1, std::min<long long>(16, EnvLongLong("VSTAT_THREADS", 8)));
    const long long limit = EnvLongLong("VSTAT_LIMIT", 0);

    Shared sh;
    sh.dry_run = EnvLongLong("VSTAT_DRY_RUN", 0) != 0;

    Pgsql main_db;
    PGconn *conn = main_db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    std::string date;
    if (const char *d = std::getenv("VSTAT_DATE"); d != nullptr && *d != '\0')
        date = d;
    else
    {
        PGresult *r = PQexec(conn, "SELECT (current_date - 1)::text");
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) != 1)
        {
            std::cerr << "Chyba zistenia datumu: " << PQerrorMessage(conn) << std::endl;
            PQclear(r);
            return 1;
        }
        date = PQgetvalue(r, 0, 0);
        PQclear(r);
    }

    if (!LoadIds(conn, full, date, limit, &sh.ids))
        return 1;

    std::vector<std::unique_ptr<Pgsql>> dbs;
    for (long long i = 0; i < threads; i++)
        dbs.emplace_back(new Pgsql());

    std::cout << "PlayersVehicleStat: start, rezim=" << mode << (full ? "" : ", den=" + date)
              << ", hracov=" << sh.ids.size() << ", vlakien=" << threads << (sh.dry_run ? ", DRY RUN" : "")
              << std::endl;

    sh.t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (long long i = 0; i < threads; i++)
        workers.emplace_back(Worker, std::ref(sh), std::ref(*dbs[static_cast<size_t>(i)]));
    for (auto &w : workers)
        w.join();

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
    std::cout << "Hotovo: hracov " << sh.players.load() << ", vlozenych tankov " << sh.inserted.load()
              << ", aktualizovanych " << sh.updated.load() << ", mrtvych uctov " << sh.dead.load()
              << ", zlyhanych davok " << sh.failed.load() << ", cas " << secs << " s" << std::endl;

    std::ostringstream info;
    info << "STATISTIKA SPRACOVANIA Typ=PlayersVehicleStat Rezim=" << mode << " Hraci=" << sh.players.load()
         << " Vlozenych=" << sh.inserted.load() << " Aktualizovanych=" << sh.updated.load()
         << " Mrtvych=" << sh.dead.load() << " Preskocenych=" << sh.failed.load();
    cron_log.SetInfo(info.str());

    if (sh.db_error)
        return 1;
    return sh.failed > 0 ? 2 : 0;
}
