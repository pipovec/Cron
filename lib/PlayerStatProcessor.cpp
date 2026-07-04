#include "./PlayerStatProcessor.h"

#include <array>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <sys/time.h>
#include <vector>

#include "./CronLog.h"
#include "./Pgsql.h"
#include "./SendCurl.h"
#include "../json/src/json.hpp"

namespace PlayerStatProcessing
{

using namespace std;

typedef unsigned long long timestamp_t;
typedef vector<int> container;

static timestamp_t GetTimestamp()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_usec + (timestamp_t)now.tv_sec * 2000000;
}

static inline string StmtSelect(const string &table) { return table + "_sel"; }
static inline string StmtInsert(const string &table) { return table + "_ins"; }
static inline string StmtUpdate(const string &table) { return table + "_upd"; }
static inline string StmtHistory(const string &table) { return table + "_his"; }

template <size_t N>
static PGresult *ExecPreparedN(PGconn *conn, const string &stmt, const array<string, N> &values)
{
    array<const char *, N> params;
    for (size_t i = 0; i < N; i++)
    {
        params[i] = values[i].c_str();
    }
    return PQexecPrepared(conn, stmt.c_str(), static_cast<int>(N), params.data(), nullptr, nullptr, 0);
}

// Zdroj account_id (parametrizovany count/select dotazom zo zdrojovej tabulky)
class AccountSource
{
public:
    PGconn *conn;
    std::unique_ptr<Pgsql> pg;

    AccountSource(string count_query, string select_query)
        : count_query_(std::move(count_query)), select_query_(std::move(select_query))
    {
        pg.reset(new Pgsql());
        this->conn = pg->Get();
        this->CheckConnection(this->conn);
    }

    int GetPocet()
    {
        PGresult *result = PQexec(this->conn, count_query_.c_str());
        this->CheckResult(result);

        string res = PQgetvalue(result, 0, 0);
        PQclear(result);

        return stoi(res);
    }

    void GetPlayers(int offset, container *account_ids, int *ntuples)
    {
        PGresult *result;
        // Obalene ako subquery, aby sme vedeli vyfiltrovat uz zname vymazane
        // ucty bez ohladu na to, z akej zdrojovej tabulky account_id pochadza.
        string query = "SELECT src.account_id FROM (" + select_query_ + to_string(offset) +
                       " LIMIT 500) src WHERE NOT EXISTS "
                       "(SELECT 1 FROM players_dead pd WHERE pd.account_id = src.account_id)";
        result = PQexec(this->conn, query.c_str());
        this->CheckResult(result);
        int riadkov = PQntuples(result);
        *ntuples = riadkov;

        account_ids->clear();
        account_ids->reserve(riadkov);
        for (int i = 0; i < riadkov; i++)
        {
            account_ids->push_back(stoi(PQgetvalue(result, i, 0)));
        }
        PQclear(result);
    }

    ~AccountSource() = default;

private:
    string count_query_;
    string select_query_;

    void CheckConnection(PGconn *connection)
    {
        if (PQstatus(connection) != CONNECTION_OK)
        {
            fprintf(stderr, "Connection to database failed: %s", PQerrorMessage(connection));
        }
    }
    void CheckResult(PGresult *result)
    {
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "SET failed: %s", PQerrorMessage(this->conn));
            PQclear(result);
        }
    }
};

// Rozsekam int account_id na jeden string so 100 account_id oddeleny ciarkami
static void GetAccountId(size_t start, size_t end, const container &account_ids, string *ids)
{
    ids->clear();
    for (size_t i = start; i < end && i < account_ids.size(); i++)
    {
        *ids += to_string(account_ids[i]) + ",";
    }
    if (!ids->empty())
    {
        ids->pop_back(); // Vymaze poslednu ciarku
    }
}

// Poslanie dat account_id na server a ziskanie JSON
static void SendPost(const string &aids, string *json)
{
    string field = "&fields=-statistics.historical,-statistics.clan,-statistics.regular_team,-statistics.fallout,-statistics.team,-statistics.company,-clan_id,-global_rating,-client_language,-last_battle_time,-created_at,-updated_at";
    string extra = "&extra=statistics.globalmap_absolute,statistics.globalmap_champion";
    const string method = "/account/info/";

    if (!aids.empty())
    {
        string post_data = field + extra + "&account_id=" + aids;
        SendCurl send;
        *json = send.SendWOT(method, post_data);
    }
}

// Vyberie len udaje relevantne pre vypocet WN8 a avgXP per battle
static void ExtractBattleStats(const string &data, int *Stat, const string &table)
{
    using json = nlohmann::json;
    json j;

    try
    {
        j = json::parse(data);
    }
    catch (json::parse_error &e)
    {
        cout << "Parser v ExtractBattleStats: " << e.what() << endl;
    }

    if (!j.is_null())
    {
        try
        {
            Stat[0] = j["statistics"][table]["damage_dealt"];
            Stat[1] = j["statistics"][table]["spotted"];
            Stat[2] = j["statistics"][table]["frags"];
            Stat[3] = j["statistics"][table]["dropped_capture_points"];
            Stat[4] = j["statistics"][table]["battles"];
            Stat[5] = j["statistics"][table]["wins"];
            Stat[6] = j["statistics"][table]["battle_avg_xp"];
        }
        catch (json::parse_error &e)
        {
            cout << "Prazdna hodnota pre: " << table << " Chyba: " << e.what() << endl;
        }
    }
}

static void PrepareTableStatements(const string &table, PGconn *conn)
{
    PGresult *result;

    const string stmt_sel = StmtSelect(table);
    const string q_sel = "SELECT damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp FROM " + table + " WHERE account_id = $1";
    result = PQprepare(conn, stmt_sel.c_str(), q_sel.c_str(), 1, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        fprintf(stderr, "Prepared statement %s chybny: %s", stmt_sel.c_str(), PQerrorMessage(conn));
    PQclear(result);

    const string stmt_ins = StmtInsert(table);
    const string q_ins = "INSERT INTO " + table + "(damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,account_id) VALUES ($1,$2,$3,$4,$5,$6,$7,$8)";
    result = PQprepare(conn, stmt_ins.c_str(), q_ins.c_str(), 8, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        fprintf(stderr, "Prepared statement %s chybny: %s", stmt_ins.c_str(), PQerrorMessage(conn));
    PQclear(result);

    const string stmt_upd = StmtUpdate(table);
    const string q_upd = "UPDATE " + table + " SET damage_dealt=$1,spotted=$2,frags=$3,dropped_capture_points=$4,battles=$5,wins=$6,battle_avg_xp=$7 WHERE account_id=$8";
    result = PQprepare(conn, stmt_upd.c_str(), q_upd.c_str(), 8, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        fprintf(stderr, "Prepared statement %s chybny: %s", stmt_upd.c_str(), PQerrorMessage(conn));
    PQclear(result);

    const string stmt_his = StmtHistory(table);
    const string q_his = "INSERT INTO " + table + "_history (damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,date,account_id) VALUES ($1,$2,$3,$4,$5,$6,$7, now() - interval '1 day', $8) ON CONFLICT DO NOTHING";
    result = PQprepare(conn, stmt_his.c_str(), q_his.c_str(), 8, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        fprintf(stderr, "Prepared statement %s chybny: %s", stmt_his.c_str(), PQerrorMessage(conn));
    PQclear(result);
}

static void ExecPreparedStatement(const string &table, PGconn *conn, const string &account_id, int *riadkov, int *pdStat)
{
    PGresult *result;
    const char *paramValues[1];
    paramValues[0] = account_id.c_str();
    *riadkov = 0;

    const string stmt_name = StmtSelect(table);
    result = PQexecPrepared(conn, stmt_name.c_str(), 1, paramValues, NULL, NULL, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        fprintf(stderr, "Select prepared statment je chybny: %s", PQresultErrorMessage(result));

    *riadkov = PQntuples(result);

    if (*riadkov > 0)
    {
        for (int i = 0; i < 7; i++)
        {
            pdStat[i] = stoi(PQgetvalue(result, 0, i));
        }
    }
    else
    {
        for (int i = 0; i < 7; i++)
        {
            pdStat[i] = 0;
        }
    }

    PQclear(result);
}

static void OnlyInsert(PGconn *conn, const string &table, int *pStat, const string &account_id)
{
    PGresult *result;
    const string stmt_name = StmtInsert(table);
    array<string, 8> values = {to_string(pStat[0]), to_string(pStat[1]), to_string(pStat[2]),
                               to_string(pStat[3]), to_string(pStat[4]), to_string(pStat[5]),
                               to_string(pStat[6]), account_id};
    result = ExecPreparedN(conn, stmt_name, values);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        cout << "Chyba OnlyInsert do " << table + ": " << PQresultErrorMessage(result) << endl;
    PQclear(result);
}

static void Upsert(PGconn *conn, const string &table, int *pStat, int *pdStat, const string &account_id)
{
    PGresult *result;
    if ((pStat[4] - pdStat[4]) > 0)
    {
        const string stmt_name = StmtHistory(table);
        array<string, 8> values = {to_string(pStat[0] - pdStat[0]), to_string(pStat[1] - pdStat[1]),
                                   to_string(pStat[2] - pdStat[2]), to_string(pStat[3] - pdStat[3]),
                                   to_string(pStat[4] - pdStat[4]), to_string(pStat[5] - pdStat[5]),
                                   to_string(pStat[6] - pdStat[6]), account_id};
        result = ExecPreparedN(conn, stmt_name, values);
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            cout << "Chyba insertu do " << table + "_history: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
    }

    const string stmt_name_upd = StmtUpdate(table);
    array<string, 8> values = {to_string(pStat[0]), to_string(pStat[1]), to_string(pStat[2]),
                               to_string(pStat[3]), to_string(pStat[4]), to_string(pStat[5]),
                               to_string(pStat[6]), account_id};
    result = ExecPreparedN(conn, stmt_name_upd, values);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        cout << "Chyba update do " << table << PQresultErrorMessage(result) << endl;
    PQclear(result);
}

static void UpsertAllTables(PGconn *conn, const string &account_id, int *pStatAll, int *pStatDef,
                             int *pStatSkir, int *pStatGMa, int *pStatGMc)
{
    int riadkov;
    int dStatAll[7], dStatGMc[7], dStatGMa[7], dStatSkir[7], dStatDef[7];

    ExecPreparedStatement("players_stat_all", conn, account_id, &riadkov, dStatAll);
    if (riadkov == 0)
        OnlyInsert(conn, "players_stat_all", pStatAll, account_id);
    else
        Upsert(conn, "players_stat_all", pStatAll, dStatAll, account_id);

    ExecPreparedStatement("players_stat_defense", conn, account_id, &riadkov, dStatDef);
    if (riadkov == 0)
        OnlyInsert(conn, "players_stat_defense", pStatDef, account_id);
    else
        Upsert(conn, "players_stat_defense", pStatDef, dStatDef, account_id);

    ExecPreparedStatement("players_stat_skirmish", conn, account_id, &riadkov, dStatSkir);
    if (riadkov == 0)
        OnlyInsert(conn, "players_stat_skirmish", pStatSkir, account_id);
    else
        Upsert(conn, "players_stat_skirmish", pStatSkir, dStatSkir, account_id);

    ExecPreparedStatement("players_stat_gm_absolute", conn, account_id, &riadkov, dStatGMa);
    if (riadkov == 0)
        OnlyInsert(conn, "players_stat_gm_absolute", pStatGMa, account_id);
    else
        Upsert(conn, "players_stat_gm_absolute", pStatGMa, dStatGMa, account_id);

    ExecPreparedStatement("players_stat_gm_champion", conn, account_id, &riadkov, dStatGMc);
    if (riadkov == 0)
        OnlyInsert(conn, "players_stat_gm_champion", pStatGMc, account_id);
    else
        Upsert(conn, "players_stat_gm_champion", pStatGMc, dStatGMc, account_id);
}

// Hrac uz na serveri neexistuje (WoT API vrati "data":{"<account_id>":null}
// pre vymazane ucty) - zaznamenam ho do players_dead
static void InsertDeadPlayer(PGconn *conn, const string &account_id)
{
    const string query = "INSERT INTO players_dead (account_id, deleted_at) VALUES ($1, now()) ON CONFLICT (account_id) DO NOTHING";
    const char *paramValues[1] = {account_id.c_str()};

    PGresult *result = PQexecParams(conn, query.c_str(), 1, NULL, paramValues, NULL, NULL, 0);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        cout << "Chyba insertu do players_dead: " << PQresultErrorMessage(result) << endl;
    PQclear(result);
}

// Spracovanie JSON odpovede zo servera pre az 100 hracov naraz.
// Vracia pocet uctov, ktore server nahlasil ako vymazane (null).
static int ProcessJson(const string &json_data, PGconn *conn)
{
    if (json_data.empty())
    {
        return 0;
    }

    using json = nlohmann::json;
    json js;

    try
    {
        js = json::parse(json_data);
    }
    catch (json::parse_error &e)
    {
        cout << "Parser 0: " << e.what() << endl;
        return 0;
    }

    js = js["data"];

    int dead_count = 0;

    PGresult *tx = PQexec(conn, "BEGIN");
    PQclear(tx);

    for (auto &x : js.items())
    {
        string account_id = x.key();
        json j = x.value();

        if (j.is_null())
        {
            // Ucet bol vymazany na serveri - nema zmysel z neho citat statistiky
            InsertDeadPlayer(conn, account_id);
            dead_count++;
            continue;
        }

        string data = j.dump();

        int StatAll[7], StatGMc[7], StatGMa[7], StatSkir[7], StatDef[7];
        ExtractBattleStats(data, StatAll, "all");
        ExtractBattleStats(data, StatGMc, "globalmap_champion");
        ExtractBattleStats(data, StatGMa, "globalmap_absolute");
        ExtractBattleStats(data, StatDef, "stronghold_defense");
        ExtractBattleStats(data, StatSkir, "stronghold_skirmish");

        UpsertAllTables(conn, account_id, StatAll, StatDef, StatSkir, StatGMa, StatGMc);
    }

    PGresult *txc = PQexec(conn, "COMMIT");
    PQclear(txc);

    return dead_count;
}

// Vycisti databazu od zaznamov starsich ako 2 mesiace
static void CleanUpHistory()
{
    Pgsql db;
    PGconn *conn = db.Get();

    const char *sql = "DELETE FROM players_stat_all_history WHERE date < now() - interval '2 months'; VACUUM ANALYZE players_stat_all_history; ";
    const char *sql1 = "DELETE FROM players_stat_defense_history WHERE date < now() - interval '2 months'; VACUUM ANALYZE players_stat_defense_history;";
    const char *sql2 = "DELETE FROM players_stat_gm_absolute_history WHERE date < now() - interval '2 months'; VACUUM ANALYZE  players_stat_gm_absolute_history;";
    const char *sql3 = "DELETE FROM players_stat_gm_champion_history WHERE date < now() - interval '2 months'; VACUUM ANALYZE players_stat_gm_champion_history;";
    const char *sql4 = "DELETE FROM players_stat_skirmish_history WHERE date < now() - interval '2 months'; VACUUM ANALYZE players_stat_skirmish_history;";

    PQexec(conn, sql);
    PQexec(conn, sql1);
    PQexec(conn, sql2);
    PQexec(conn, sql3);
    PQexec(conn, sql4);
}

int RunJob(const JobConfig &config)
{
    CronLog::ScopedRun cron_log(config.cron_log_name);

    timestamp_t t0 = GetTimestamp();
    time_t start, stop;
    time(&start);
    cout << endl
         << "Program zacal pracovat: " << ctime(&start) << endl;

    AccountSource players(config.count_query, config.select_query);
    int riadkov = players.GetPocet();

    if (config.apply_env_limit)
    {
        const char *limit_env = std::getenv("PLAYERSSTAT_LIMIT");
        if (limit_env && *limit_env)
        {
            int limit = std::atoi(limit_env);
            if (limit > 0 && limit < riadkov)
            {
                riadkov = limit;
            }
        }
    }

    cout << "Pocet riadkov " << riadkov << endl;
    cout << "QUERY " << config.select_query << endl;

    container a_ids;

    PrepareTableStatements("players_stat_all", players.conn);
    PrepareTableStatements("players_stat_defense", players.conn);
    PrepareTableStatements("players_stat_skirmish", players.conn);
    PrepareTableStatements("players_stat_gm_absolute", players.conn);
    PrepareTableStatements("players_stat_gm_champion", players.conn);

    int ntuples = 0;
    int spracovanych = 0;
    int kolo = 0;
    int mrtvi = 0;

    for (int i = 0; i < riadkov; i += 500)
    {
        kolo++;
        players.GetPlayers(i, &a_ids, &ntuples);

        if (ntuples < 500)
            cout << "pocet hracov na spracovanie: " << ntuples << endl;

        for (size_t start_idx = 0; start_idx < a_ids.size(); start_idx += 100)
        {
            size_t end_idx = start_idx + 100;
            string aids;
            string json_response;
            GetAccountId(start_idx, end_idx, a_ids, &aids);
            if (!aids.empty())
            {
                SendPost(aids, &json_response);
                mrtvi += ProcessJson(json_response, players.conn);
            }
        }

        spracovanych += ntuples;
        ntuples = 0;
    }

    if (mrtvi > 0)
    {
        cout << "Vymazanych uctov najdenych: " << mrtvi << endl;
    }

    if (config.run_cleanup)
    {
        CleanUpHistory();
    }

    timestamp_t t2 = GetTimestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    std::ostringstream info;
    info << std::fixed << std::setprecision(3);
    info << "STATISTIKA SPRACOVANIA "
         << "Typ=" << config.stat_type_name
         << " Kola=" << kolo
         << " Cas=" << secs << "s"
         << " Klany=0"
         << " Hraci=" << spracovanych
         << " Prichody=0"
         << " Odchody=0"
         << " NoveID=0"
         << " Nicky=0"
         << " Ukoncene=0"
         << " Updaty=0"
         << " Inserty=0"
         << " Spracovane=" << spracovanych
         << " MissingStats=0"
         << " MissingVehicles=0"
         << " Neaktivne=0"
         << " MrtviHraci=" << mrtvi;
    cron_log.SetInfo(info.str());

    return 0;
}

} // namespace PlayerStatProcessing
