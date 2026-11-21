/*
 * PlayersVehicleSnapshot - jednorazovy bootstrap tabulky player_vehicle_snapshot
 *
 * Prejde vsetkych hracov z players_all (okrem players_dead), pre kazdu davku 100 uctov
 * stiahne /account/tanks/ a ulozi aktualny stav bitiek/vyhier na kazdom tanku. Prirastky
 * sa pri tom NEzapisuju - tabulka sluzi ako vychodzi stav pre denny vypocet prirastkov.
 *
 * Premenne prostredia:
 *   VEHICLE_AFTER_ACCOUNT_ID  pokracovat za danym account_id (obnovenie po prerusenom behu)
 *   VEHICLE_LIMIT             spracovat najviac N hracov (na testovanie), 0 = vsetkych
 *
 * Pri zlyhani davky vypise "ZLYHALA DAVKA po account_id=X" - rovnaku davku zopakujes
 * spustenim s VEHICLE_AFTER_ACCOUNT_ID=X.
 *
 * Najprv vytvor tabulky: psql -d clan -f sql/001_player_vehicle_tables.sql
 */

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
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

// Dalsia davka account_id za 'after' (keyset), bez uz znamych vymazanych uctov
static bool NextBatch(PGconn *conn, long long after, std::vector<long long> *ids)
{
    ids->clear();
    const std::string after_s = std::to_string(after);
    const char *params[1] = {after_s.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "SELECT pa.account_id FROM players_all pa "
        "WHERE pa.account_id > $1::int "
        "AND NOT EXISTS (SELECT 1 FROM players_dead pd WHERE pd.account_id = pa.account_id) "
        "ORDER BY pa.account_id LIMIT 100",
        1, nullptr, params, nullptr, nullptr, 0);

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

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersVehicleSnapshot");

    Pgsql db;
    PGconn *conn = db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    long long after = EnvLongLong("VEHICLE_AFTER_ACCOUNT_ID", 0);
    const long long limit = EnvLongLong("VEHICLE_LIMIT", 0);

    std::cout << "PlayersVehicleSnapshot: start, po account_id=" << after
              << (limit > 0 ? ", limit=" + std::to_string(limit) : "") << std::endl;

    SendCurl send;
    const auto t0 = std::chrono::steady_clock::now();

    long long players = 0, rows = 0, batches = 0, failed = 0;
    std::vector<long long> ids;

    while (limit == 0 || players < limit)
    {
        if (!NextBatch(conn, after, &ids))
            return 1;
        if (ids.empty())
            break;

        PlayerVehicles::AccountTanks tanks;
        if (!PlayerVehicles::FetchAccountTanks(send, ids, &tanks))
        {
            std::cout << "ZLYHALA DAVKA po account_id=" << after << std::endl;
            failed++;
        }
        else
        {
            long long n = PlayerVehicles::UpsertSnapshot(conn, tanks);
            if (n < 0)
            {
                std::cout << "ZLYHALA DAVKA po account_id=" << after << " (zapis)" << std::endl;
                failed++;
            }
            else
            {
                rows += n;
            }
        }

        players += static_cast<long long>(ids.size());
        after = ids.back();
        batches++;

        if (batches % 100 == 0)
        {
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::cout << "hracov " << players << ", riadkov " << rows << ", davok " << batches
                      << ", posledne account_id=" << after << ", " << static_cast<long long>(secs) << " s"
                      << std::endl;
        }
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Hotovo: hracov " << players << ", riadkov " << rows << ", davok " << batches
              << ", zlyhanych davok " << failed << ", cas " << secs << " s, posledne account_id=" << after
              << std::endl;

    std::ostringstream info;
    info << "STATISTIKA SPRACOVANIA Typ=PlayersVehicleSnapshot Hraci=" << players << " Riadky=" << rows
         << " Davky=" << batches << " Chyby=" << failed;
    cron_log.SetInfo(info.str());

    return failed > 0 ? 2 : 0;
}
