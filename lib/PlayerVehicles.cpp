#include "./PlayerVehicles.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

namespace PlayerVehicles
{

using namespace std;
using json = nlohmann::json;

// Cislo z JSON objektu alebo 0, ak chyba / nie je cislo (bez vynimiek)
static int NumberOr0(const json &obj, const char *key)
{
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number())
        return 0;
    return it->get<int>();
}

bool ParseAccountTanks(const string &raw, AccountTanks *out)
{
    out->clear();

    json js = json::parse(raw, nullptr, false);
    if (js.is_discarded() || !js.is_object())
        return false;

    auto status = js.find("status");
    if (status == js.end() || !status->is_string() || status->get<string>() != "ok")
        return false;

    auto data = js.find("data");
    if (data == js.end() || !data->is_object())
        return false;

    for (auto &x : data->items())
    {
        if (!x.value().is_array() || x.value().empty())
            continue; // null = ucet bez tankov / neexistuje

        long long account_id;
        try
        {
            account_id = stoll(x.key());
        }
        catch (const exception &)
        {
            continue;
        }

        vector<TankRow> rows;
        rows.reserve(x.value().size());
        for (const auto &t : x.value())
        {
            auto tank_id = t.find("tank_id");
            auto stats = t.find("statistics");
            if (tank_id == t.end() || !tank_id->is_number_integer() ||
                stats == t.end() || !stats->is_object())
                continue;

            rows.push_back({tank_id->get<int>(), NumberOr0(*stats, "battles"), NumberOr0(*stats, "wins")});
        }

        if (!rows.empty())
            (*out)[account_id] = std::move(rows);
    }

    return true;
}

bool FetchAccountTanks(SendCurl &send, const vector<long long> &ids, AccountTanks *out)
{
    out->clear();
    if (ids.empty())
        return true;

    string aids;
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (i)
            aids += ',';
        aids += to_string(ids[i]);
    }

    const string post = "&fields=tank_id,statistics.battles,statistics.wins&account_id=" + aids;

    string raw;
    try
    {
        raw = send.SendWOT("/account/tanks/", post);
    }
    catch (const string &e)
    {
        cerr << "FetchAccountTanks: " << e << endl;
        return false;
    }
    catch (const exception &e)
    {
        cerr << "FetchAccountTanks: " << e.what() << endl;
        return false;
    }

    if (!ParseAccountTanks(raw, out))
    {
        cerr << "FetchAccountTanks: neplatna odpoved API" << endl;
        return false;
    }
    return true;
}

static string PgArray(const vector<long long> &v)
{
    string s = "{";
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i)
            s += ',';
        s += to_string(v[i]);
    }
    s += '}';
    return s;
}

long long UpsertSnapshot(PGconn *conn, const AccountTanks &tanks)
{
    vector<long long> ids, tank_ids, battles, wins;
    for (const auto &[account_id, rows] : tanks)
    {
        for (const TankRow &r : rows)
        {
            ids.push_back(account_id);
            tank_ids.push_back(r.tank_id);
            battles.push_back(r.battles);
            wins.push_back(r.wins);
        }
    }
    if (ids.empty())
        return 0;

    const string a = PgArray(ids), b = PgArray(tank_ids), c = PgArray(battles), d = PgArray(wins);
    const char *params[4] = {a.c_str(), b.c_str(), c.c_str(), d.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "INSERT INTO player_vehicle_snapshot (account_id, tank_id, battles, wins) "
        "SELECT v.a, v.t, v.b, v.w FROM unnest($1::bigint[],$2::bigint[],$3::bigint[],$4::bigint[]) AS v(a,t,b,w) "
        "WHERE true "
        "ON CONFLICT (account_id, tank_id) DO UPDATE "
        "SET battles = EXCLUDED.battles, wins = EXCLUDED.wins, updated_at = now() "
        "WHERE (player_vehicle_snapshot.battles, player_vehicle_snapshot.wins) "
        "IS DISTINCT FROM (EXCLUDED.battles, EXCLUDED.wins)",
        4, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cerr << "UpsertSnapshot: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return -1;
    }

    long long n = atoll(PQcmdTuples(result));
    PQclear(result);
    return n;
}

DeltaResult ComputeDeltas(const TankState &old, const vector<TankRow> &now)
{
    DeltaResult result;
    for (const TankRow &t : now)
    {
        int old_battles = 0, old_wins = 0;
        auto it = old.find(t.tank_id);
        if (it != old.end())
        {
            old_battles = it->second.first;
            old_wins = it->second.second;
        }

        int delta_battles = t.battles - old_battles;
        if (delta_battles <= 0)
            continue;

        int delta_wins = t.wins - old_wins;
        if (delta_wins < 0)
            delta_wins = 0;

        result.rows.push_back({t.tank_id, delta_battles, delta_wins});
        result.battles += delta_battles;
    }
    return result;
}

bool LooksSuspicious(long long delta_battles, long long history_battles)
{
    return delta_battles > 2 * history_battles + 50;
}

bool LoadSnapshot(PGconn *conn, const vector<long long> &ids, SnapshotMap *out)
{
    out->clear();
    if (ids.empty())
        return true;

    const string arr = PgArray(ids);
    const char *params[1] = {arr.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "SELECT account_id, tank_id, battles, wins FROM player_vehicle_snapshot "
        "WHERE account_id = ANY($1::bigint[])",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        cerr << "LoadSnapshot: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    for (int i = 0; i < n; i++)
    {
        long long account_id = stoll(PQgetvalue(result, i, 0));
        int tank_id = stoi(PQgetvalue(result, i, 1));
        int battles = PQgetisnull(result, i, 2) ? 0 : stoi(PQgetvalue(result, i, 2));
        int wins = PQgetisnull(result, i, 3) ? 0 : stoi(PQgetvalue(result, i, 3));
        (*out)[account_id][tank_id] = {battles, wins};
    }
    PQclear(result);
    return true;
}

long long UpsertDaily(PGconn *conn, const vector<DailyRow> &rows)
{
    if (rows.empty())
        return 0;

    string dates = "{";
    vector<long long> accounts, tanks, battles, wins;
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (i)
            dates += ',';
        dates += rows[i].date;
        accounts.push_back(rows[i].account_id);
        tanks.push_back(rows[i].tank_id);
        battles.push_back(rows[i].battles);
        wins.push_back(rows[i].wins);
    }
    dates += '}';

    const string a = PgArray(accounts), t = PgArray(tanks), b = PgArray(battles), w = PgArray(wins);
    const char *params[5] = {dates.c_str(), a.c_str(), t.c_str(), b.c_str(), w.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "INSERT INTO player_vehicle_daily (date, account_id, tank_id, battles, wins) "
        "SELECT v.d, v.a, v.t, v.b, v.w FROM "
        "unnest($1::date[],$2::bigint[],$3::bigint[],$4::bigint[],$5::bigint[]) AS v(d,a,t,b,w) "
        "WHERE true "
        "ON CONFLICT (date, account_id, tank_id) DO UPDATE "
        "SET battles = player_vehicle_daily.battles + EXCLUDED.battles, "
        "wins = player_vehicle_daily.wins + EXCLUDED.wins",
        5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cerr << "UpsertDaily: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return -1;
    }

    long long n = atoll(PQcmdTuples(result));
    PQclear(result);
    return n;
}

long long UpsertSync(PGconn *conn, const vector<pair<long long, string>> &sync)
{
    if (sync.empty())
        return 0;

    vector<long long> accounts;
    string dates = "{";
    for (size_t i = 0; i < sync.size(); i++)
    {
        if (i)
            dates += ',';
        accounts.push_back(sync[i].first);
        dates += sync[i].second;
    }
    dates += '}';

    const string a = PgArray(accounts);
    const char *params[2] = {a.c_str(), dates.c_str()};

    PGresult *result = PQexecParams(
        conn,
        "INSERT INTO player_vehicle_sync (account_id, synced_date) "
        "SELECT v.a, v.d FROM unnest($1::bigint[],$2::date[]) AS v(a,d) WHERE true "
        "ON CONFLICT (account_id) DO UPDATE SET synced_date = EXCLUDED.synced_date, synced_at = now()",
        2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cerr << "UpsertSync: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return -1;
    }

    long long n = atoll(PQcmdTuples(result));
    PQclear(result);
    return n;
}

} // namespace PlayerVehicles
