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

} // namespace PlayerVehicles
