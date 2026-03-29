#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <array>
#include <libpq-fe.h>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <sys/time.h>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include "./json/src/json.hpp"
#include <vector>

typedef unsigned long long timestamp_t;
typedef std::vector<int> container;

static timestamp_t get_timestamp()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_usec + (timestamp_t)now.tv_sec * 2000000;
}

using namespace std;

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

class Players
{
public:
    PGconn *conn;
    std::unique_ptr<Pgsql> pg;

    Players()
    {
        pg.reset(new Pgsql());
        this->conn = pg->Get();
        this->CheckConnection(this->conn);
    }

    string basic_query_count = "SELECT count(account_id) FROM aktualne_odchody";
    string basic_query = "SELECT account_id FROM aktualne_odchody OFFSET ";

    int GetPocet()
    {
        PGresult *result;
        string res;

        result = PQexec(this->conn, basic_query_count.c_str());
        this->CheckResult(result);

        res = PQgetvalue(result, 0, 0);
        PQclear(result);

        return stoi(res);
    }

    void GetPlayers(int offset, container *account_ids, int *ntuples)
    {
        PGresult *result;
        string query = basic_query + to_string(offset) + " LIMIT 500";
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

    ~Players() = default;

private:
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

void GetAccountId(size_t start, size_t end, const container &account_ids, string *ids)
{
    ids->clear();
    for (size_t i = start; i < end && i < account_ids.size(); i++)
    {
        *ids += to_string(account_ids[i]) + ",";
    }
    if (!ids->empty())
    {
        ids->pop_back();
    }
}

void SendPost(const string &aids, string *json)
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

void PlayersStat(string data, int *Stat, string table)
{
    using json = nlohmann::json;
    json j;

    try
    {
        j = json::parse(data);
    }
    catch (json::parse_error &e)
    {
        cout << "Parser v PlayersStat: " << e.what() << endl;
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

    j = NULL;
}

void PrepareTableStatements(const string &table, PGconn *conn)
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

void ExecPreparedStatment(string table, PGconn *conn, string account_id, int *riadkov, int *pdStat)
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
        *(pdStat + 0) = stoi(PQgetvalue(result, 0, 0));
        *(pdStat + 1) = stoi(PQgetvalue(result, 0, 1));
        *(pdStat + 2) = stoi(PQgetvalue(result, 0, 2));
        *(pdStat + 3) = stoi(PQgetvalue(result, 0, 3));
        *(pdStat + 4) = stoi(PQgetvalue(result, 0, 4));
        *(pdStat + 5) = stoi(PQgetvalue(result, 0, 5));
        *(pdStat + 6) = stoi(PQgetvalue(result, 0, 6));
    }
    else
    {
        *(pdStat + 0) = *(pdStat + 1) = *(pdStat + 2) = *(pdStat + 3) = *(pdStat + 4) = *(pdStat + 5) = *(pdStat + 6) = 0;
    }

    PQclear(result);
}

void OnlyInsert(PGconn *conn, string table, int *pStat, string account_id)
{
    PGresult *result;
    const string stmt_name = StmtInsert(table);
    array<string, 8> values = {to_string(*(pStat + 0)), to_string(*(pStat + 1)), to_string(*(pStat + 2)),
                               to_string(*(pStat + 3)), to_string(*(pStat + 4)), to_string(*(pStat + 5)),
                               to_string(*(pStat + 6)), account_id};
    result = ExecPreparedN(conn, stmt_name, values);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        cout << "Chyba OnlyInsert do " << table + ": " << PQresultErrorMessage(result) << endl;
    PQclear(result);
}

void Upsert(PGconn *conn, string table, int *pStat, int *pdStat, string account_id)
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

void Json(const string &json_data, PGconn *conn)
{
    if (!json_data.empty())
    {
        string data, account_id;
        int riadkov;
        int StatAll[7], StatGMc[7], StatGMa[7], StatSkir[7], StatDef[7];
        int dStatAll[7], dStatGMc[7], dStatGMa[7], dStatSkir[7], dStatDef[7];

        int *priadkov = &riadkov;
        int *pStatAll = StatAll, *pStatGMc = StatGMc, *pStatGMa = StatGMa;
        int *pStatSkir = StatSkir, *pStatDef = StatDef;
        int *pdStatAll = dStatAll, *pdStatGMc = dStatGMc, *pdStatGMa = dStatGMa;
        int *pdStatSkir = dStatSkir, *pdStatDef = dStatDef;

        using json = nlohmann::json;
        json js;

        try
        {
            js = json::parse(json_data);
        }
        catch (json::parse_error &e)
        {
            cout << "Parser 0: " << e.what() << endl;
            return;
        }

        js = js["data"];

        PGresult *tx = PQexec(conn, "BEGIN");
        if (PQresultStatus(tx) != PGRES_COMMAND_OK)
            PQclear(tx);
        else
            PQclear(tx);

        for (auto &x : js.items())
        {
            account_id = x.key();
            json j = x.value();
            data = j.dump();

            PlayersStat(data, pStatAll, "all");
            PlayersStat(data, pStatGMc, "globalmap_champion");
            PlayersStat(data, pStatGMa, "globalmap_absolute");
            PlayersStat(data, pStatDef, "stronghold_defense");
            PlayersStat(data, pStatSkir, "stronghold_skirmish");

            ExecPreparedStatment("players_stat_all", conn, account_id, priadkov, pdStatAll);
            if (riadkov == 0)
                OnlyInsert(conn, "players_stat_all", pStatAll, account_id);
            else
                Upsert(conn, "players_stat_all", pStatAll, pdStatAll, account_id);

            ExecPreparedStatment("players_stat_defense", conn, account_id, priadkov, pdStatDef);
            if (riadkov == 0)
                OnlyInsert(conn, "players_stat_defense", pStatDef, account_id);
            else
                Upsert(conn, "players_stat_defense", pStatDef, pdStatDef, account_id);

            ExecPreparedStatment("players_stat_skirmish", conn, account_id, priadkov, pdStatSkir);
            if (riadkov == 0)
                OnlyInsert(conn, "players_stat_skirmish", pStatSkir, account_id);
            else
                Upsert(conn, "players_stat_skirmish", pStatSkir, pdStatSkir, account_id);

            ExecPreparedStatment("players_stat_gm_absolute", conn, account_id, priadkov, pdStatGMa);
            if (riadkov == 0)
                OnlyInsert(conn, "players_stat_gm_absolute", pStatGMa, account_id);
            else
                Upsert(conn, "players_stat_gm_absolute", pStatGMa, pdStatGMa, account_id);

            ExecPreparedStatment("players_stat_gm_champion", conn, account_id, priadkov, pdStatGMc);
            if (riadkov == 0)
                OnlyInsert(conn, "players_stat_gm_champion", pStatGMc, account_id);
            else
                Upsert(conn, "players_stat_gm_champion", pStatGMc, pdStatGMc, account_id);
        }

        PGresult *txc = PQexec(conn, "COMMIT");
        if (PQresultStatus(txc) != PGRES_COMMAND_OK)
            PQclear(txc);
        else
            PQclear(txc);

        js = NULL;
    }
}

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:PlayersStatAktualneOdchody");

    timestamp_t t0 = get_timestamp();
    time_t start, stop;
    time(&start);
    cout << endl
         << "Program zacal pracovat: " << ctime(&start) << endl;

    Players players;
    int riadkov = players.GetPocet();
    cout << "Pocet riadkov " << riadkov << endl;
    cout << "QUERY " << players.basic_query << endl;

    container a_ids;

    PrepareTableStatements("players_stat_all", players.conn);
    PrepareTableStatements("players_stat_defense", players.conn);
    PrepareTableStatements("players_stat_skirmish", players.conn);
    PrepareTableStatements("players_stat_gm_absolute", players.conn);
    PrepareTableStatements("players_stat_gm_champion", players.conn);

    int ntuples;
    int *p_ntuples = &ntuples;
    int spracovanych = 0;
    int kolo = 0;

    for (int i = 0; i < riadkov; i += 500)
    {
        kolo++;
        players.GetPlayers(i, &a_ids, p_ntuples);

        if (ntuples < 500)
            cout << "pocet hracov na spracovanie: " << ntuples << endl;

        for (size_t start = 0; start < a_ids.size(); start += 100)
        {
            size_t end = start + 100;
            string aids;
            string json;
            GetAccountId(start, end, a_ids, &aids);
            if (!aids.empty())
            {
                SendPost(aids, &json);
                Json(json, players.conn);
            }
        }

        spracovanych += ntuples;
        ntuples = 0;
    }

    timestamp_t t2 = get_timestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    std::ostringstream info;
    info << std::fixed << std::setprecision(3);
    info << "STATISTIKA SPRACOVANIA "
         << "Typ=PlayersStatAktualneOdchody"
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
         << " Neaktivne=0";
    cron_log.SetInfo(info.str());
    return 0;
}
