/*
 * PlayersVehicleDelta - denne prirastky bitiek/vyhier po tankoch
 *
 * Spusta sa po PlayersStatAll (ten plni players_stat_all_history). Pre kazdeho hraca, ktory ma
 * v historii novsi riadok nez je jeho player_vehicle_sync.synced_date:
 *   1. stiahne /account/tanks/ (davky po 100 uctoch),
 *   2. porovna s player_vehicle_snapshot a zapise rozdiely do player_vehicle_daily
 *      (pod datum najnovsieho riadka historie, ktory prirastok pokryva),
 *   3. aktualizuje snapshot a synced_date.
 * Vsetky tri zapisy davky bezia v jednej transakcii, takze pri vypadku sa nic nezapocita dvakrat.
 *
 * Hraci, ktori este nemaju riadok v player_vehicle_sync (novi hraci, hraci mimo bootstrapu),
 * dostanu len vychodzi stav (snapshot + sync) a ziadne prirastky.
 *
 * Kontrola voci historii: sucet prirastkov bitiek po tankoch sa porovna s bitkami v
 * players_stat_all_history za rovnake obdobie. Ak je prirastok neprimerane velky (napr. predany
 * tank, ktory sa vratil so starymi statistikami), prirastky hraca sa nezapisu (snapshot a sync sa
 * aktualizuju) a hrac sa spocita ako "podozrivy".
 *
 * Premenne prostredia:
 *   DELTA_THREADS         pocet vlakien (default 4, 1 az 16)
 *   DELTA_LIMIT           spracovat najviac N hracov (na testovanie), 0 = vsetkych
 *   DELTA_LOOKBACK_DAYS   ako daleko do historie hladat hracov na spracovanie (default 40)
 *   DELTA_KEEP_DAYS       kolko dni prirastkov drzat v player_vehicle_daily (default 35)
 *   DELTA_DRY_RUN=1       len vypise, kolko hracov by sa spracovalo (bez API a bez zapisov)
 *
 * Najprv: psql -f sql/001_player_vehicle_tables.sql, sql/002_player_vehicle_sync.sql a
 * jednorazovy bootstrap (PlayersVehicleSnapshot).
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

static const long long kBatchSize = 100;
static const long long kAdvisoryLockKey = 727001; // zabrani dvom beziacim instanciam naraz

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

struct Candidate
{
    long long account_id;
    std::string new_date;       // najnovsi datum historie, ktory prirastok pokryva
    long long history_battles;  // bitky v historii za obdobie od synced_date
    bool baseline;              // bez riadka v player_vehicle_sync -> len vychodzi stav
};

// Nacita hracov na spracovanie. baseline = true pre hracov bez riadka v player_vehicle_sync.
static bool LoadCandidates(PGconn *conn, int lookback_days, bool baseline, std::vector<Candidate> *out)
{
    const std::string lookback = std::to_string(lookback_days);
    const char *params[1] = {lookback.c_str()};

    const char *synced_sql =
        "SELECT s.account_id, max(h.date)::text, sum(h.battles)::bigint "
        "FROM player_vehicle_sync s "
        "JOIN players_stat_all_history h ON h.account_id = s.account_id AND h.date > s.synced_date "
        "WHERE h.date >= current_date - $1::int "
        "GROUP BY s.account_id ORDER BY s.account_id";

    const char *baseline_sql =
        "SELECT h.account_id, max(h.date)::text, sum(h.battles)::bigint "
        "FROM players_stat_all_history h "
        "WHERE h.date >= current_date - $1::int "
        "AND NOT EXISTS (SELECT 1 FROM player_vehicle_sync s WHERE s.account_id = h.account_id) "
        "GROUP BY h.account_id ORDER BY h.account_id";

    PGresult *result = PQexecParams(conn, baseline ? baseline_sql : synced_sql, 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba nacitania hracov: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    out->reserve(out->size() + n);
    for (int i = 0; i < n; i++)
    {
        Candidate c;
        c.account_id = std::stoll(PQgetvalue(result, i, 0));
        c.new_date = PQgetvalue(result, i, 1);
        c.history_battles = PQgetisnull(result, i, 2) ? 0 : std::stoll(PQgetvalue(result, i, 2));
        c.baseline = baseline;
        out->push_back(std::move(c));
    }
    PQclear(result);
    return true;
}

static bool Exec(PGconn *conn, const char *sql)
{
    PGresult *r = PQexec(conn, sql);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK;
    if (!ok)
        std::cerr << "Chyba '" << sql << "': " << PQerrorMessage(conn) << std::endl;
    PQclear(r);
    return ok;
}

struct Shared
{
    std::vector<Candidate> cands;
    std::atomic<size_t> next{0};

    std::atomic<long long> processed{0}, failed_batches{0}, baseline{0}, no_tanks{0}, suspicious{0};
    std::atomic<long long> with_delta{0}, daily_rows{0}, delta_battles{0}, history_battles{0};
    std::atomic<long long> batches{0};
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

        while (true)
        {
            size_t begin = sh.next.fetch_add(static_cast<size_t>(kBatchSize));
            if (begin >= sh.cands.size())
                return;
            size_t end = std::min(begin + static_cast<size_t>(kBatchSize), sh.cands.size());

            std::vector<long long> ids;
            for (size_t i = begin; i < end; i++)
                ids.push_back(sh.cands[i].account_id);

            PlayerVehicles::AccountTanks tanks;
            PlayerVehicles::SnapshotMap snapshot;
            if (!PlayerVehicles::FetchAccountTanks(send, ids, &tanks) ||
                !PlayerVehicles::LoadSnapshot(conn, ids, &snapshot))
            {
                sh.failed_batches++;
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "ZLYHALA DAVKA (od account_id=" << ids.front() << ", " << ids.size()
                          << " hracov, spracuju sa pri dalsom spusteni)" << std::endl;
                continue;
            }

            std::vector<PlayerVehicles::DailyRow> daily;
            std::vector<std::pair<long long, std::string>> sync;
            long long l_baseline = 0, l_no_tanks = 0, l_suspicious = 0, l_with_delta = 0;
            long long l_delta_battles = 0, l_history_battles = 0;

            for (size_t i = begin; i < end; i++)
            {
                const Candidate &c = sh.cands[i];
                sync.emplace_back(c.account_id, c.new_date);

                auto t = tanks.find(c.account_id);
                if (t == tanks.end())
                {
                    l_no_tanks++;
                    continue;
                }
                if (c.baseline)
                {
                    l_baseline++;
                    continue;
                }

                PlayerVehicles::TankState empty;
                auto s = snapshot.find(c.account_id);
                PlayerVehicles::DeltaResult d =
                    PlayerVehicles::ComputeDeltas(s == snapshot.end() ? empty : s->second, t->second);

                if (PlayerVehicles::LooksSuspicious(d.battles, c.history_battles))
                {
                    l_suspicious++;
                    std::lock_guard<std::mutex> lock(sh.print_mtx);
                    std::cout << "PODOZRIVY account_id=" << c.account_id << ": prirastok po tankoch " << d.battles
                              << " bitiek, historia " << c.history_battles << " - prirastky sa nezapisu" << std::endl;
                    continue;
                }

                for (const PlayerVehicles::TankRow &r : d.rows)
                    daily.push_back({c.new_date, c.account_id, r.tank_id, r.battles, r.wins});

                if (!d.rows.empty())
                    l_with_delta++;
                l_delta_battles += d.battles;
                l_history_battles += c.history_battles;
            }

            // Prirastky, snapshot a sync v jednej transakcii - pri vypadku sa nic nezapocita dvakrat
            bool ok = Exec(conn, "BEGIN");
            if (ok)
            {
                ok = PlayerVehicles::UpsertDaily(conn, daily) >= 0 &&
                     PlayerVehicles::UpsertSnapshot(conn, tanks) >= 0 &&
                     PlayerVehicles::UpsertSync(conn, sync) >= 0;
                if (ok)
                    ok = Exec(conn, "COMMIT");
                else
                    Exec(conn, "ROLLBACK");
            }

            if (!ok)
            {
                sh.failed_batches++;
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "ZLYHAL ZAPIS DAVKY (od account_id=" << ids.front()
                          << ", spracuje sa pri dalsom spusteni)" << std::endl;
                continue;
            }

            sh.processed += static_cast<long long>(ids.size());
            sh.baseline += l_baseline;
            sh.no_tanks += l_no_tanks;
            sh.suspicious += l_suspicious;
            sh.with_delta += l_with_delta;
            sh.daily_rows += static_cast<long long>(daily.size());
            sh.delta_battles += l_delta_battles;
            sh.history_battles += l_history_battles;

            long long b = ++sh.batches;
            if (b % 100 == 0)
            {
                double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
                std::lock_guard<std::mutex> lock(sh.print_mtx);
                std::cout << "hracov " << sh.processed.load() << " z " << sh.cands.size() << ", davok " << b << ", "
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

// Zmaze prirastky starsie ako keep_days po davkach (samostatne transakcie, cez primarny kluc)
static long long CleanupDaily(PGconn *conn, int keep_days)
{
    const long long kChunk = 50000;
    const std::string query =
        "WITH d AS (DELETE FROM player_vehicle_daily t USING ("
        "SELECT date, account_id, tank_id FROM player_vehicle_daily "
        "WHERE date < current_date - " + std::to_string(keep_days) + " LIMIT " + std::to_string(kChunk) +
        ") x WHERE t.date = x.date AND t.account_id = x.account_id AND t.tank_id = x.tank_id RETURNING 1) "
        "SELECT count(*) FROM d";

    long long total = 0;
    for (;;)
    {
        PGresult *r = PQexec(conn, query.c_str());
        if (PQresultStatus(r) != PGRES_TUPLES_OK)
        {
            std::cerr << "Chyba cistenia player_vehicle_daily: " << PQresultErrorMessage(r) << std::endl;
            PQclear(r);
            break;
        }
        long long n = std::stoll(PQgetvalue(r, 0, 0));
        PQclear(r);
        total += n;
        if (n < kChunk)
            break;
    }
    return total;
}

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersVehicleDelta");

    const long long threads = std::max<long long>(1, std::min<long long>(16, EnvLongLong("DELTA_THREADS", 4)));
    const long long limit = EnvLongLong("DELTA_LIMIT", 0);
    const int lookback = static_cast<int>(std::max<long long>(1, EnvLongLong("DELTA_LOOKBACK_DAYS", 40)));
    const int keep_days = static_cast<int>(std::max<long long>(1, EnvLongLong("DELTA_KEEP_DAYS", 35)));
    const bool dry_run = EnvLongLong("DELTA_DRY_RUN", 0) != 0;

    Pgsql main_db;
    PGconn *conn = main_db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    // Ochrana pred sucasnym behom dvoch instancii (dvojite zapocitanie prirastkov)
    {
        PGresult *r = PQexec(conn, ("SELECT pg_try_advisory_lock(" + std::to_string(kAdvisoryLockKey) + ")").c_str());
        bool locked = PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) == 1 && std::string(PQgetvalue(r, 0, 0)) == "t";
        PQclear(r);
        if (!locked)
        {
            std::cerr << "PlayersVehicleDelta uz bezi (advisory lock), koncim" << std::endl;
            return 3;
        }
    }

    Shared sh;
    std::vector<Candidate> synced, baseline;
    if (!LoadCandidates(conn, lookback, false, &synced) || !LoadCandidates(conn, lookback, true, &baseline))
        return 1;

    std::cout << "PlayersVehicleDelta: hracov s novou historiou " << synced.size() << ", bez synchronizacie (baseline) "
              << baseline.size() << ", lookback " << lookback << " dni" << std::endl;

    if (dry_run)
    {
        long long hb = 0;
        for (const Candidate &c : synced)
            hb += c.history_battles;
        std::cout << "DRY RUN: bitiek v historii pre synchronizovanych hracov " << hb << ", API sa nevola, nic sa nezapisuje"
                  << std::endl;
        return 0;
    }

    sh.cands = std::move(synced);
    sh.cands.insert(sh.cands.end(), baseline.begin(), baseline.end());
    if (limit > 0 && static_cast<long long>(sh.cands.size()) > limit)
        sh.cands.resize(static_cast<size_t>(limit));

    std::vector<std::unique_ptr<Pgsql>> dbs;
    for (long long i = 0; i < threads; i++)
        dbs.emplace_back(new Pgsql());

    std::cout << "start, vlakien=" << threads << ", na spracovanie " << sh.cands.size() << std::endl;
    sh.t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (long long i = 0; i < threads; i++)
        workers.emplace_back(Worker, std::ref(sh), std::ref(*dbs[i]));
    for (auto &w : workers)
        w.join();

    const long long cleaned = CleanupDaily(conn, keep_days);

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - sh.t0).count();
    double coverage = sh.history_battles > 0 ? 100.0 * sh.delta_battles / sh.history_battles : 0.0;

    std::cout << "Hotovo: hracov " << sh.processed.load() << ", baseline " << sh.baseline.load() << ", bez tankov "
              << sh.no_tanks.load() << ", podozrivych " << sh.suspicious.load() << ", s prirastkom "
              << sh.with_delta.load() << ", riadkov daily " << sh.daily_rows.load() << ", zlyhanych davok "
              << sh.failed_batches.load() << ", cas " << secs << " s" << std::endl;
    std::cout << "Pokrytie: prirastok po tankoch " << sh.delta_battles.load() << " bitiek, historia "
              << sh.history_battles.load() << " bitiek (" << coverage << " %), zmazanych starych daily riadkov " << cleaned
              << std::endl;

    std::ostringstream info;
    info << "STATISTIKA SPRACOVANIA Typ=PlayersVehicleDelta Hraci=" << sh.processed.load()
         << " Baseline=" << sh.baseline.load() << " Podozrive=" << sh.suspicious.load()
         << " Riadky=" << sh.daily_rows.load() << " Pokrytie=" << static_cast<int>(coverage)
         << " Chyby=" << sh.failed_batches.load();
    cron_log.SetInfo(info.str());

    if (sh.db_error)
        return 1;
    return sh.failed_batches > 0 ? 2 : 0;
}
