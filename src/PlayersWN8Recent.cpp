/*
 * PlayersWN8Recent - WN8 hracov za posledne 7/14/28 dni
 *
 * Spusta sa po PlayersVehicleDelta. Pre kazde obdobie:
 *   - skutocne sucty (damage, spotted, frags, dcp, wins, battles) zo players_stat_all_history,
 *   - ocakavane sucty = sum(expected_tank_value * prirastok bitiek tanku) z player_vehicle_daily,
 *   - WN8 sa vypocita spolocnym vzorcom (lib/Wn8Formula), rovnakym ako v PlayersWN8v2.
 * Ťazka agregacia ide v SQL (mnozinova operacia nad desiatkami milionov riadkov), do aplikacie
 * prichadza len par cisel na hraca.
 *
 * Vysledok sa zapise do wn8player_recent len pre hracov, ktori maju dost bitiek a dostatocne
 * pokrytie dat (sucet prirastkov po tankoch / bitky v historii). Ostatnym sa riadok zmaze.
 *
 * Premenne prostredia:
 *   WN8R_PERIODS        obdobia v dnoch, oddelene ciarkou (default 7,14,28)
 *   WN8R_MIN_BATTLES    minimum bitiek v obdobi (default 20)
 *   WN8R_MIN_COVERAGE   minimalne pokrytie v % (default 90)
 *   WN8R_END_DATE       posledny den obdobia YYYY-MM-DD (default: najnovsi den v player_vehicle_daily)
 *   WN8R_DRY_RUN=1      vypise statistiku a rozdelenie, nic nezapise
 *
 * Najprv: psql -f sql/003_wn8player_recent.sql
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "./lib/CronLog.h"
#include "./lib/Pgsql.h"
#include "./lib/Wn8Formula.h"

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

static std::vector<int> ParsePeriods(const char *text)
{
    std::vector<int> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        try
        {
            int d = std::stoi(item);
            if ((d == 7 || d == 14 || d == 28) && std::find(out.begin(), out.end(), d) == out.end())
                out.push_back(d);
        }
        catch (...)
        {
        }
    }
    return out;
}

struct Row
{
    long long account_id;
    long long battles;
    double wn8;
    double coverage;
};

static std::string Num(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// Agregacia za jedno obdobie. Vracia false pri chybe dotazu.
static bool LoadPeriod(PGconn *conn, const std::string &end_date, int days, long long min_battles,
                       double min_coverage, std::vector<Row> *rows, long long *candidates,
                       long long *low_battles, long long *low_coverage, long long *low_expected)
{
    rows->clear();
    *candidates = *low_battles = *low_coverage = *low_expected = 0;

    const std::string days_s = std::to_string(days);
    const char *params[2] = {end_date.c_str(), days_s.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "WITH h AS ("
        "  SELECT account_id, sum(battles)::bigint AS b, sum(wins)::bigint AS w, sum(damage_dealt)::bigint AS dmg,"
        "         sum(spotted)::bigint AS sp, sum(frags)::bigint AS fr, sum(dropped_capture_points)::bigint AS df"
        "  FROM players_stat_all_history"
        "  WHERE date BETWEEN $1::date - ($2::int - 1) AND $1::date GROUP BY account_id"
        "), d AS ("
        "  SELECT dd.account_id, sum(dd.battles)::bigint AS tb,"
        "         coalesce(sum(e.dmg  * dd.battles), 0) AS ed, coalesce(sum(e.spot * dd.battles), 0) AS es,"
        "         coalesce(sum(e.frag * dd.battles), 0) AS ef, coalesce(sum(e.def  * dd.battles), 0) AS edf,"
        "         coalesce(sum(0.01 * e.win * dd.battles), 0) AS ew,"
        "         coalesce(sum(dd.battles) FILTER (WHERE e.tank_id IS NOT NULL), 0)::bigint AS eb,"
        "         coalesce(sum(dd.wins)    FILTER (WHERE e.tank_id IS NULL), 0)::bigint AS uw"
        "  FROM player_vehicle_daily dd LEFT JOIN expected_tank_value e ON e.tank_id = dd.tank_id"
        "  WHERE dd.date BETWEEN $1::date - ($2::int - 1) AND $1::date GROUP BY dd.account_id"
        ") "
        "SELECT h.account_id, h.b, d.tb, h.w, h.dmg, h.sp, h.fr, h.df, d.ed, d.es, d.ef, d.edf, d.ew, d.eb, d.uw "
        "FROM h JOIN d ON d.account_id = h.account_id ORDER BY h.account_id",
        2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba agregacie pre " << days << " dni: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    rows->reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++)
    {
        (*candidates)++;
        long long account_id = std::stoll(PQgetvalue(result, i, 0));
        long long battles = std::stoll(PQgetvalue(result, i, 1));
        long long tank_battles = std::stoll(PQgetvalue(result, i, 2));

        if (battles < min_battles)
        {
            (*low_battles)++;
            continue;
        }

        double coverage = battles > 0 ? 100.0 * static_cast<double>(tank_battles) / static_cast<double>(battles) : 0.0;
        if (coverage < min_coverage)
        {
            (*low_coverage)++;
            continue;
        }

        // Tanky bez ocakavanych hodnot (expected_tank_value) nema z coho vyhodnotit. Ak ich je viac nez
        // (100 - min_coverage) % bitiek, hrac sa vyradi. Inak sa ich bitky a vyhry odpocitaju presne a
        // damage/spot/frags/def (v historii len sucet za hraca, nie po tankoch) sa znizia pomerne.
        long long exp_battles = std::stoll(PQgetvalue(result, i, 13));
        double exp_coverage = 100.0 * static_cast<double>(exp_battles) / static_cast<double>(battles);
        if (exp_coverage < min_coverage)
        {
            (*low_expected)++;
            continue;
        }
        const double share = std::min(1.0, static_cast<double>(exp_battles) / static_cast<double>(battles));

        Wn8::Totals actual, expected;
        actual.wins = std::max(0.0, std::stod(PQgetvalue(result, i, 3)) - std::stod(PQgetvalue(result, i, 14)));
        actual.dmg = std::stod(PQgetvalue(result, i, 4)) * share;
        actual.spot = std::stod(PQgetvalue(result, i, 5)) * share;
        actual.frags = std::stod(PQgetvalue(result, i, 6)) * share;
        actual.def = std::stod(PQgetvalue(result, i, 7)) * share;
        expected.dmg = std::stod(PQgetvalue(result, i, 8));
        expected.spot = std::stod(PQgetvalue(result, i, 9));
        expected.frags = std::stod(PQgetvalue(result, i, 10));
        expected.def = std::stod(PQgetvalue(result, i, 11));
        expected.wins = std::stod(PQgetvalue(result, i, 12));

        // Bez ocakavanych hodnot (napr. vsetky tanky mimo expected_tank_value) nie je z coho pocitat
        if (expected.dmg <= 0.0 || expected.spot <= 0.0 || expected.frags <= 0.0 || expected.def <= 0.0 ||
            expected.wins <= 0.0)
        {
            (*low_coverage)++;
            continue;
        }

        float wn8 = Wn8::Compute(actual, expected);
        if (!std::isfinite(wn8))
            continue;

        rows->push_back({account_id, battles, static_cast<double>(wn8), std::min(coverage, 9999.99)});
    }
    PQclear(result);
    return true;
}

// Zapise vysledky po davkach (unnest) a zmaze riadky, ktore tento beh neobnovil
static bool SaveRows(PGconn *conn, int days, const std::string &end_date, const std::vector<Row> &rows)
{
    // Cas zaciatku zapisu: riadky s updated_at starsim nez toto sa na konci zmazu
    PGresult *tr = PQexec(conn, "SELECT clock_timestamp()::text");
    if (PQresultStatus(tr) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba zistenia casu: " << PQerrorMessage(conn) << std::endl;
        PQclear(tr);
        return false;
    }
    const std::string run_start = PQgetvalue(tr, 0, 0);
    PQclear(tr);

    const std::string days_s = std::to_string(days);
    const size_t kChunk = 5000;

    for (size_t begin = 0; begin < rows.size(); begin += kChunk)
    {
        size_t end = std::min(begin + kChunk, rows.size());
        std::string ids = "{", wn8 = "{", battles = "{", coverage = "{";
        for (size_t i = begin; i < end; i++)
        {
            if (i != begin)
            {
                ids += ',';
                wn8 += ',';
                battles += ',';
                coverage += ',';
            }
            ids += std::to_string(rows[i].account_id);
            wn8 += Num(rows[i].wn8);
            battles += std::to_string(rows[i].battles);
            coverage += Num(rows[i].coverage);
        }
        ids += '}';
        wn8 += '}';
        battles += '}';
        coverage += '}';

        const char *params[6] = {days_s.c_str(), end_date.c_str(), ids.c_str(), wn8.c_str(), battles.c_str(), coverage.c_str()};
        PGresult *r = PQexecParams(
            conn,
            "INSERT INTO wn8player_recent (account_id, period, wn8, battles, coverage, date_from, date_to) "
            "SELECT v.a, $1::smallint, v.w, v.b, v.c, $2::date - ($1::int - 1), $2::date "
            "FROM unnest($3::bigint[], $4::numeric[], $5::bigint[], $6::numeric[]) AS v(a, w, b, c) "
            "WHERE true "
            "ON CONFLICT (account_id, period) DO UPDATE SET wn8 = EXCLUDED.wn8, battles = EXCLUDED.battles, "
            "coverage = EXCLUDED.coverage, date_from = EXCLUDED.date_from, date_to = EXCLUDED.date_to, updated_at = now()",
            6, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
        {
            std::cerr << "Chyba zapisu (" << days << " dni): " << PQresultErrorMessage(r) << std::endl;
            PQclear(r);
            return false;
        }
        PQclear(r);
    }

    // Hraci, ktori uz podmienky nespĺnaju (alebo vypadli z historie), nemaju mat stary vysledok
    const char *del_params[2] = {days_s.c_str(), run_start.c_str()};
    PGresult *d = PQexecParams(conn, "DELETE FROM wn8player_recent WHERE period = $1::smallint AND updated_at < $2::timestamptz",
                               2, nullptr, del_params, nullptr, nullptr, 0);
    if (PQresultStatus(d) != PGRES_COMMAND_OK)
    {
        std::cerr << "Chyba mazania starych riadkov: " << PQresultErrorMessage(d) << std::endl;
        PQclear(d);
        return false;
    }
    std::cout << "  zmazanych neaktualnych riadkov: " << PQcmdTuples(d) << std::endl;
    PQclear(d);
    return true;
}

static void PrintDistribution(std::vector<Row> rows)
{
    if (rows.empty())
        return;
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.wn8 < b.wn8; });
    auto q = [&](double p) { return rows[static_cast<size_t>(p * (rows.size() - 1))].wn8; };
    double sum = 0;
    for (const Row &r : rows)
        sum += r.wn8;
    std::cout << "  WN8: priemer " << Num(sum / rows.size()) << ", min " << Num(rows.front().wn8) << ", p10 "
              << Num(q(0.10)) << ", median " << Num(q(0.50)) << ", p90 " << Num(q(0.90)) << ", max "
              << Num(rows.back().wn8) << std::endl;
}

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersWN8Recent");

    const char *periods_env = std::getenv("WN8R_PERIODS");
    std::vector<int> periods = ParsePeriods(periods_env && *periods_env ? periods_env : "7,14,28");
    const long long min_battles = EnvLongLong("WN8R_MIN_BATTLES", 20);
    const double min_coverage = static_cast<double>(EnvLongLong("WN8R_MIN_COVERAGE", 90));
    const bool dry_run = EnvLongLong("WN8R_DRY_RUN", 0) != 0;

    if (periods.empty())
    {
        std::cerr << "WN8R_PERIODS neobsahuje platne obdobie (7, 14, 28)" << std::endl;
        return 1;
    }

    Pgsql db;
    PGconn *conn = db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    // Posledny den obdobia: najnovsi den s dennymi prirastkami po tankoch (dovtedy je cele WN8 podlozene)
    std::string end_date;
    const char *end_env = std::getenv("WN8R_END_DATE");
    if (end_env && *end_env)
    {
        end_date = end_env;
    }
    else
    {
        PGresult *r = PQexec(conn, "SELECT max(date)::text FROM player_vehicle_daily");
        if (PQresultStatus(r) != PGRES_TUPLES_OK)
        {
            std::cerr << "Chyba zistenia posledneho dna: " << PQerrorMessage(conn) << std::endl;
            PQclear(r);
            return 1;
        }
        if (PQgetisnull(r, 0, 0))
        {
            PQclear(r);
            std::cout << "player_vehicle_daily je prazdna - zatial nie je z coho pocitat" << std::endl;
            return 0;
        }
        end_date = PQgetvalue(r, 0, 0);
        PQclear(r);
    }

    std::cout << "PlayersWN8Recent: koniec obdobia " << end_date << ", min bitiek " << min_battles << ", min pokrytie "
              << min_coverage << " %" << (dry_run ? " (DRY RUN)" : "") << std::endl;

    std::ostringstream info;
    info << "STATISTIKA SPRACOVANIA Typ=PlayersWN8Recent Do=" << end_date;
    bool failed = false;

    for (int days : periods)
    {
        std::vector<Row> rows;
        long long candidates, low_battles, low_coverage, low_expected;
        if (!LoadPeriod(conn, end_date, days, min_battles, min_coverage, &rows, &candidates, &low_battles, &low_coverage, &low_expected))
        {
            failed = true;
            continue;
        }

        std::cout << days << " dni: hracov s dennymi prirastkami " << candidates << ", malo bitiek " << low_battles
                  << ", nizke pokrytie " << low_coverage << ", bez ocakavanych hodnot " << low_expected << ", vysledkov " << rows.size() << std::endl;
        PrintDistribution(rows);
        info << " P" << days << "=" << rows.size();

        if (!dry_run && !SaveRows(conn, days, end_date, rows))
            failed = true;
    }

    cron_log.SetInfo(info.str());
    return failed ? 2 : 0;
}
