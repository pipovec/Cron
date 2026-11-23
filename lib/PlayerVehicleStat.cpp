#include "./PlayerVehicleStat.h"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>

namespace PlayerVehicleStat
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

bool Parse(const string &raw, Batch *out)
{
    out->rows.clear();
    out->dead_accounts = 0;

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
        if (x.value().is_null())
        {
            out->dead_accounts++;
            continue;
        }
        if (!x.value().is_array())
            continue;

        int account_id;
        try
        {
            account_id = stoi(x.key());
        }
        catch (const exception &)
        {
            continue;
        }

        for (const auto &t : x.value())
        {
            auto tank_id = t.find("tank_id");
            auto stats = t.find("statistics");
            if (tank_id == t.end() || !tank_id->is_number_integer() ||
                stats == t.end() || !stats->is_object())
                continue;

            out->rows.push_back({account_id, tank_id->get<int>(), NumberOr0(*stats, "battles"),
                                 NumberOr0(*stats, "wins"), NumberOr0(t, "mark_of_mastery")});
        }
    }

    return true;
}

bool Fetch(SendCurl &send, const vector<int> &ids, Batch *out)
{
    out->rows.clear();
    out->dead_accounts = 0;
    if (ids.empty())
        return true;

    string aids;
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (i)
            aids += ',';
        aids += to_string(ids[i]);
    }

    const string post = "&fields=tank_id,mark_of_mastery,statistics.battles,statistics.wins&account_id=" + aids;

    string raw;
    try
    {
        raw = send.SendWOT("/account/tanks/", post);
    }
    catch (const string &e)
    {
        cerr << "PlayerVehicleStat::Fetch: " << e << endl;
        return false;
    }
    catch (const exception &e)
    {
        cerr << "PlayerVehicleStat::Fetch: " << e.what() << endl;
        return false;
    }

    if (!Parse(raw, out))
    {
        cerr << "PlayerVehicleStat::Fetch: neplatna odpoved API" << endl;
        return false;
    }
    return true;
}

static void Append(string &s, int v)
{
    if (s.size() > 1)
        s += ',';
    s += to_string(v);
}

bool Upsert(PGconn *conn, const vector<Row> &rows, UpsertCount *count)
{
    *count = UpsertCount();
    if (rows.empty())
        return true;

    string a = "{", t = "{", b = "{", w = "{", m = "{";
    for (const Row &r : rows)
    {
        Append(a, r.account_id);
        Append(t, r.tank_id);
        Append(b, r.battles);
        Append(w, r.wins);
        Append(m, r.mark_of_mastery);
    }
    a += '}';
    t += '}';
    b += '}';
    w += '}';
    m += '}';

    const char *params[5] = {a.c_str(), t.c_str(), b.c_str(), w.c_str(), m.c_str()};

    // xmax sa na partitionovanej tabulke vratit nedá, preto sa pocet uz existujucich riadkov zisti v
    // tom istom prikaze (CTE "ex" vidi stav pred zapisom): vlozene = vsetky - existujuce,
    // aktualizovane = zapisane - vlozene.
    PGresult *result = PQexecParams(
        conn,
        "WITH v AS ("
        "  SELECT * FROM unnest($1::int[],$2::int[],$3::int[],$4::int[],$5::int[]) AS v(a,t,b,w,m)"
        "), ex AS MATERIALIZED ("
        "  SELECT 1 FROM v JOIN player_vehicle_stat p ON p.account_id = v.a AND p.tank_id = v.t"
        "), up AS ("
        "  INSERT INTO player_vehicle_stat (account_id, tank_id, battles, wins, mark_of_mastery) "
        "  SELECT a, t, b, w, m FROM v "
        "  ON CONFLICT (account_id, tank_id) DO UPDATE "
        "  SET battles = EXCLUDED.battles, wins = EXCLUDED.wins, mark_of_mastery = EXCLUDED.mark_of_mastery "
        "  WHERE (player_vehicle_stat.battles, player_vehicle_stat.mark_of_mastery) "
        "  IS DISTINCT FROM (EXCLUDED.battles, EXCLUDED.mark_of_mastery) "
        "  RETURNING 1"
        ") "
        "SELECT (SELECT count(*) FROM v), (SELECT count(*) FROM ex), (SELECT count(*) FROM up)",
        5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) != 1)
    {
        cerr << "PlayerVehicleStat::Upsert: " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return false;
    }

    const long long total = atoll(PQgetvalue(result, 0, 0));
    const long long existing = atoll(PQgetvalue(result, 0, 1));
    const long long written = atoll(PQgetvalue(result, 0, 2));
    count->inserted = total - existing;
    count->updated = written - count->inserted;
    PQclear(result);
    return true;
}

} // namespace PlayerVehicleStat
