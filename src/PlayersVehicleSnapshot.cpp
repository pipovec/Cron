/*
 * PlayersVehicleSnapshot - jednorazovy bootstrap tabulky player_vehicle_snapshot
 *
 * Prejde vsetkych hracov z players_all (okrem players_dead), pre kazdu davku 100 uctov
 * stiahne /account/tanks/ a ulozi aktualny stav bitiek/vyhier na kazdom tanku. Prirastky
 * sa pri tom NEzapisuju - tabulka sluzi ako vychodzi stav pre denny vypocet prirastkov.
 *
 * Bezi vo viacerych vlaknach (kazde ma vlastne spojenie do DB aj vlastny curl). Davky sa
 * beru pod mutexom z jedneho kurzora (keyset podla account_id).
 *
 * Pre kazdeho spracovaneho hraca (aj bez tankov) zapise riadok do player_vehicle_sync s datumom
 * posledneho riadka players_stat_all_history, ktory je uz v snapshote zahrnuty (stav k zaciatku
 * behu). Podla toho denny job (PlayersVehicleDelta) vie, od ktoreho dna ma pocitat prirastky.
 *
 * Obnovenie: v predvolenom rezime sa vynechaju ucty, ktore uz maju riadok v player_vehicle_sync -
 * po prerusenom alebo castocne zlyhanom behu staci program spustit znova.
 *
 * Premenne prostredia:
 *   VEHICLE_THREADS           pocet vlakien (default 4, 1 az 16)
 *   VEHICLE_LIMIT             spracovat najviac N hracov (na testovanie), 0 = vsetkych
 *   VEHICLE_AFTER_ACCOUNT_ID  zacat za danym account_id (default 0)
 *   VEHICLE_REFRESH=1         nevynechavat ucty, ktore uz maju riadok v player_vehicle_sync
 *                             (upsert zmeni len riadky snapshotu, kde sa battles/wins zmenili)
 *
 * Najprv vytvor tabulky: psql -d clan -f sql/001_player_vehicle_tables.sql
 *                        psql -d clan -f sql/002_player_vehicle_sync.sql
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "./lib/CronLog.h"
#include "./lib/Pgsql.h"
#include "./lib/PlayerVehicles.h"
#include "./lib/SendCurl.h"

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

// Dalsia davka account_id za 'after' (keyset), bez uz znamych vymazanych uctov.
// Ak skip_existing, vynechava aj ucty, ktore uz maju riadok v player_vehicle_sync.
static bool NextBatch(PGconn *conn, long long after, long long want, bool skip_existing,
                      std::vector<long long> *ids)
{
    ids->clear();
    const std::string after_s = std::to_string(after);
    const std::string want_s = std::to_string(want);
    const char *params[2] = {after_s.c_str(), want_s.c_str()};

    const std::string query =
        std::string("SELECT pa.account_id FROM players_all pa "
                    "WHERE pa.account_id > $1::int "
                    "AND NOT EXISTS (SELECT 1 FROM players_dead pd WHERE pd.account_id = pa.account_id) ") +
        (skip_existing ? "AND NOT EXISTS (SELECT 1 FROM player_vehicle_sync s WHERE s.account_id = pa.account_id) "
                       : "") +
        "ORDER BY pa.account_id LIMIT $2::int";

    PGresult *result = PQexecParams(conn, query.c_str(), 2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba vyberu uctov: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    ids->reserve(n);
    for (int i = 0; i < n; i++)
        ids->push_back(std::stoll(PQgetvalue(result, i, 0)));
    PQclear(result);
    return true;
}

// Stav zdielany medzi vlaknami
struct Shared
{
    std::mutex cursor_mtx;          // chrani kurzor a dispatched
    PGconn *cursor_conn = nullptr;  // spojenie len na vyber davok (pouziva sa pod cursor_mtx)
    long long after = 0;
    long long dispatched = 0;
    long long limit = 0;
    bool skip_existing = true;
    bool done = false;
    std::string synced_date;        // datum posledneho riadka historie zahrnuteho v snapshote

    std::atomic<long long> last_after{0};
    std::atomic<long long> players{0}, rows{0}, batches{0}, failed{0};
    std::atomic<bool> db_error{false};

    std::mutex print_mtx;
    std::chrono::steady_clock::time_point t0;
};

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

        std::vector<long long> ids;
        while (true)
        {
            long long first_after;
            {
                std::lock_guard<std::mutex> lock(sh.cursor_mtx);
                if (sh.done)
                    return;

                long long want = 100;
                if (sh.limit > 0)
                {
                    want = std::min<long long>(100, sh.limit - sh.dispatched);
                    if (want <= 0)
                    {
                        sh.done = true;
                        return;
                    }
                }

                first_after = sh.after;
                if (!NextBatch(sh.cursor_conn, sh.after, want, sh.skip_existing, &ids))
                {
                    sh.db_error = true;
                    sh.done = true;
                    return;
                }
                if (ids.empty())
                {
                    sh.done = true;
                    return;
                }
                sh.after = ids.back();
                sh.dispatched += static_cast<long long>(ids.size());
                sh.last_after = sh.after;
            }

            PlayerVehicles::AccountTanks tanks;
            bool ok = PlayerVehicles::FetchAccountTanks(send, ids, &tanks);
            if (ok)
            {
                long long n = PlayerVehicles::UpsertSnapshot(conn, tanks);
                if (n < 0)
                {
                    ok = false;
                }
                else
                {
                    sh.rows += n;

                    // Sync riadok aj pre hracov bez tankov - tych uz netreba pri obnoveni znova sahat
                    std::vector<std::pair<long long, std::string>> sync;
                    sync.reserve(ids.size());
                    for (long long id : ids)
                        sync.emplace_back(id, sh.synced_date);
                    if (PlayerVehicles::UpsertSync(conn, sync) < 0)
                        ok = false;
                }
            }

            if (!ok)
            {
                // Ucty ostanu bez snapshotu - pri dalsom spusteni sa spracuju znova
                sh.failed++;
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "ZLYHALA DAVKA po account_id=" << first_after << " (" << ids.size()
                          << " uctov, spracuju sa pri dalsom spusteni)" << std::endl;
            }

            sh.players += static_cast<long long>(ids.size());
            long long b = ++sh.batches;
            if (b % 100 == 0)
            {
                double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "hracov " << sh.players.load() << ", riadkov " << sh.rows.load() << ", davok " << b
                          << ", kurzor account_id=" << sh.last_after.load() << ", " << static_cast<long long>(secs)
                          << " s" << std::endl;
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

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersVehicleSnapshot");

    const long long threads = std::max<long long>(1, std::min<long long>(16, EnvLongLong("VEHICLE_THREADS", 4)));

    Shared sh;
    sh.after = EnvLongLong("VEHICLE_AFTER_ACCOUNT_ID", 0);
    sh.last_after = sh.after;
    sh.limit = EnvLongLong("VEHICLE_LIMIT", 0);
    sh.skip_existing = EnvLongLong("VEHICLE_REFRESH", 0) == 0;

    // Spojenia sa vytvaraju v hlavnom vlakne (nacitanie konfiguracie nie je potrebne robit paralelne)
    Pgsql cursor_db;
    sh.cursor_conn = cursor_db.Get();
    if (sh.cursor_conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    // Datum posledneho riadka historie - snapshot, ktory teraz stiahneme, ho uz obsahuje
    {
        PGresult *r = PQexec(sh.cursor_conn, "SELECT coalesce(max(date), current_date - 1)::text FROM players_stat_all_history");
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) != 1)
        {
            std::cerr << "Chyba zistenia datumu historie: " << PQerrorMessage(sh.cursor_conn) << std::endl;
            PQclear(r);
            return 1;
        }
        sh.synced_date = PQgetvalue(r, 0, 0);
        PQclear(r);
    }

    std::vector<std::unique_ptr<Pgsql>> dbs;
    for (long long i = 0; i < threads; i++)
        dbs.emplace_back(new Pgsql());

    std::cout << "PlayersVehicleSnapshot: start, vlakien=" << threads << ", synced_date=" << sh.synced_date
              << ", po account_id=" << sh.after
              << (sh.skip_existing ? ", preskakujem ucty uz v snapshote" : ", refresh vsetkych")
              << (sh.limit > 0 ? ", limit=" + std::to_string(sh.limit) : "") << std::endl;

    sh.t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (long long i = 0; i < threads; i++)
        workers.emplace_back(Worker, std::ref(sh), std::ref(*dbs[i]));
    for (auto &w : workers)
        w.join();

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
    std::cout << "Hotovo: hracov " << sh.players.load() << ", riadkov " << sh.rows.load() << ", davok "
              << sh.batches.load() << ", zlyhanych davok " << sh.failed.load() << ", cas " << secs
              << " s, kurzor account_id=" << sh.last_after.load() << std::endl;

    std::ostringstream info;
    info << "STATISTIKA SPRACOVANIA Typ=PlayersVehicleSnapshot Hraci=" << sh.players.load()
         << " Riadky=" << sh.rows.load() << " Davky=" << sh.batches.load() << " Chyby=" << sh.failed.load();
    cron_log.SetInfo(info.str());

    if (sh.db_error)
        return 1;
    return sh.failed > 0 ? 2 : 0;
}
