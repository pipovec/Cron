#include "./PlayerStatProcessor.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <sys/time.h>
#include <vector>

#include "./CronLog.h"
#include "./DbConfig.h"
#include "./Pgsql.h"
#include "./SendCurl.h"
#include <nlohmann/json.hpp>

namespace PlayerStatProcessing
{

using namespace std;

typedef unsigned long long timestamp_t;
typedef vector<int> container;

static timestamp_t GetTimestamp()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

// Zdroj account_id (parametrizovany nazvom zdrojovej tabulky)
class AccountSource
{
public:
    PGconn *conn;
    std::unique_ptr<Pgsql> pg;

    AccountSource(string count_query, string source_table)
        : count_query_(std::move(count_query)), source_table_(std::move(source_table))
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

    // Dalsia davka account_id za poslednym spracovanym (keyset pagination).
    //
    // Povodne to bolo OFFSET, kde Postgres musel pri kazdej davke prejst a
    // zahodit vsetky predchadzajuce riadky - pri players_all (1,8M riadkov) to
    // ku koncu behu znamenalo seq scan cez cely zdroj na kazdych 500 uctov.
    // Keyset ide priamo po indexe na account_id, takze kazda davka stoji rovnako.
    //
    // Uz zname vymazane ucty filtrujeme priamo v dotaze, aby kurzor postupoval
    // podla skutocne vratenych riadkov bez ohladu na zdrojovu tabulku.
    void GetPlayers(int after_account_id, container *account_ids, int *ntuples)
    {
        PGresult *result;
        string query = "SELECT src.account_id FROM " + source_table_ + " src "
                       "WHERE src.account_id > " + to_string(after_account_id) +
                       " AND NOT EXISTS "
                       "(SELECT 1 FROM players_dead pd WHERE pd.account_id = src.account_id) "
                       "ORDER BY src.account_id LIMIT 500";
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
    string source_table_;

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

// Poslanie dat account_id na server a ziskanie JSON.
// Pri sietovej chybe (po opakovaniach v SendWOT) vrati prazdny JSON - davka sa preskoci.
// mongo_fields: navyse statistics.clan, clan_id, global_rating, last_battle_time,
// created_at, updated_at pre dokument v MongoDB.
static void SendPost(SendCurl &send, const string &aids, string *json, bool mongo_fields)
{
    string field = mongo_fields
        ? "&fields=-statistics.historical,-statistics.regular_team,-statistics.fallout,-statistics.team,-statistics.company,-client_language"
        : "&fields=-statistics.historical,-statistics.clan,-statistics.regular_team,-statistics.fallout,-statistics.team,-statistics.company,-clan_id,-global_rating,-client_language,-last_battle_time,-created_at,-updated_at";
    string extra = "&extra=statistics.globalmap_absolute,statistics.globalmap_champion";
    const string method = "/account/info/";

    json->clear();
    if (aids.empty())
        return;

    string post_data = field + extra + "&account_id=" + aids;
    try
    {
        *json = send.SendWOT(method, post_data);
    }
    catch (const string &e)
    {
        cout << "SendPost: " << e << " - davku preskakujem" << endl;
    }
    catch (const std::exception &e)
    {
        cout << "SendPost: " << e.what() << " - davku preskakujem" << endl;
    }
}

// ---------------------------------------------------------------------------------
// MongoDB fpcstat.player-stats (predtym PHP admin cron:player-stats-mongo).
//
// Bez C/C++ drivera: na zaciatku behu mongoexport stiahne (_id, updated_at) vsetkych
// dokumentov, pocas behu sa zmenene dokumenty posielaju ako Extended JSON do rury
// "mongoimport --mode=upsert" (nahradi cely dokument podla _id). Zapisuje sa len hrac,
// ktoremu sa zmenil WG updated_at alebo v Mongo este nie je (rovnako ako PHP).
// Pripojenie: MONGODB_URL z .env (alebo premennej prostredia). Nastrojom sa odovzda cez
// docasny YAML pre --config (mkstemp, 0600, po behu sa zmaze), aby heslo nebolo vidiet
// v zozname procesov.
//   PLAYERSSTAT_NOMONGO=1  zapis do Mongo vypne
//   MONGO_FULL=1           zapise vsetkych hracov bez ohladu na updated_at
//   MONGO_COLLECTION       ina kolekcia namiesto player-stats (testovanie)
class MongoSync
{
public:
    long long written = 0;
    long long skipped = 0;

    bool Enabled() const { return pipe_ != nullptr; }

    MongoSync() = default;
    MongoSync(const MongoSync &) = delete;
    MongoSync &operator=(const MongoSync &) = delete;
    ~MongoSync()
    {
        if (pipe_ != nullptr)
            pclose(pipe_);
        RemoveTempConfig();
    }

    bool Open()
    {
        const string uri = DbConfig::Get("MONGODB_URL");
        if (uri.empty())
        {
            cout << "Mongo: MONGODB_URL nie je nastavene (.env) - zapis do Mongo vypnuty" << endl;
            return false;
        }
        if (!WriteTempConfig(uri))
            return false;
        const char *full = std::getenv("MONGO_FULL");
        full_ = full && *full && *full != '0';

        if (!full_ && !LoadUpdatedAt())
            return false;

        // Rura do mongoimport; ak importer spadne, zapis do rury nesmie zabit proces signalom
        signal(SIGPIPE, SIG_IGN);
        const string cmd = "mongoimport --config='" + config_ + "' --db=fpcstat --collection=" + Collection() +
                           " --mode=upsert --type=json --quiet --numInsertionWorkers=2";
        pipe_ = popen(cmd.c_str(), "w");
        if (pipe_ == nullptr)
        {
            cout << "Mongo: mongoimport sa nepodarilo spustit - zapis do Mongo vypnuty" << endl;
            return false;
        }
        cout << "Mongo: zapis zapnuty" << (full_ ? " (MONGO_FULL - vsetci hraci)" : "")
             << ", dokumentov v Mongo: " << updated_at_.size() << endl;
        return true;
    }

    // Zapise dokument hraca, ak sa zmenil (podla WG updated_at)
    void Write(long long account_id, const nlohmann::json &p)
    {
        if (pipe_ == nullptr)
            return;

        const long long upd = EpochOrZero(p, "updated_at");
        if (!full_)
        {
            auto it = updated_at_.find(account_id);
            if (it != updated_at_.end() && it->second == upd)
            {
                skipped++;
                return;
            }
        }

        nlohmann::json doc;
        doc["_id"] = account_id;
        doc["last_battle_time"] = DateOrNull(p, "last_battle_time");
        doc["created_at"] = DateOrNull(p, "created_at");
        doc["updated_at"] = DateOrNull(p, "updated_at");
        doc["record_updated_at"] = Date(static_cast<long long>(time(nullptr)));
        doc["global_rating"] = p.contains("global_rating") ? p["global_rating"] : nlohmann::json(nullptr);
        doc["clan_id"] = p.contains("clan_id") && p["clan_id"].is_number() ? p["clan_id"] : nlohmann::json(0);

        auto st = p.find("statistics");
        if (st != p.end() && st->is_object())
        {
            if (st->contains("all") && (*st)["all"].is_object())
                doc["all"] = (*st)["all"];
            if (st->contains("clan") && (*st)["clan"].is_object())
                doc["clan"] = (*st)["clan"];
        }

        const string line = doc.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
        if (fwrite(line.data(), 1, line.size(), pipe_) != line.size())
        {
            cout << "Mongo: zapis do mongoimport zlyhal - dalsie dokumenty sa preskocia" << endl;
            pclose(pipe_);
            pipe_ = nullptr;
            failed_ = true;
            return;
        }
        written++;
    }

    // Vracia true, ak mongoimport skoncil uspesne
    bool Close()
    {
        if (pipe_ == nullptr)
            return !failed_;
        int rc = pclose(pipe_);
        pipe_ = nullptr;
        RemoveTempConfig();
        if (rc != 0)
        {
            cout << "Mongo: mongoimport skoncil s chybou (rc=" << rc << ")" << endl;
            failed_ = true;
        }
        return !failed_;
    }

private:
    FILE *pipe_ = nullptr;
    bool full_ = false;
    bool failed_ = false;
    string config_; // cesta k docasnemu YAML s uri
    unordered_map<long long, long long> updated_at_;

    // Docasny YAML pre --config mongoexport/mongoimport (subor 0600 z mkstemp)
    bool WriteTempConfig(const string &uri)
    {
        const char *tmpdir = std::getenv("TMPDIR");
        string tmpl = string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/mongotools-XXXXXX";
        vector<char> path(tmpl.begin(), tmpl.end());
        path.push_back('\0');
        int fd = mkstemp(path.data());
        if (fd < 0)
        {
            cout << "Mongo: docasnu konfiguraciu sa nepodarilo vytvorit - zapis do Mongo vypnuty" << endl;
            return false;
        }
        config_ = path.data();

        string escaped;
        for (char ch : uri)
        {
            if (ch == '\\' || ch == '"')
                escaped += '\\';
            escaped += ch;
        }
        const string content = "uri: \"" + escaped + "\"\n";
        const bool ok = write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
        close(fd);
        if (!ok)
        {
            cout << "Mongo: zapis docasnej konfiguracie zlyhal - zapis do Mongo vypnuty" << endl;
            RemoveTempConfig();
        }
        return ok;
    }

    void RemoveTempConfig()
    {
        if (!config_.empty())
        {
            unlink(config_.c_str());
            config_.clear();
        }
    }

    // MONGO_COLLECTION prepise cielovu kolekciu (na testovanie), povolene len [A-Za-z0-9_-]
    static string Collection()
    {
        const char *c = std::getenv("MONGO_COLLECTION");
        if (c == nullptr || *c == '\0' || strspn(c, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(c))
            return "player-stats";
        return c;
    }

    static nlohmann::json Date(long long epoch_s)
    {
        return {{"$date", {{"$numberLong", to_string(epoch_s * 1000)}}}};
    }
    static long long EpochOrZero(const nlohmann::json &p, const char *key)
    {
        auto it = p.find(key);
        return (it != p.end() && it->is_number()) ? it->get<long long>() : 0;
    }
    static nlohmann::json DateOrNull(const nlohmann::json &p, const char *key)
    {
        auto it = p.find(key);
        if (it == p.end() || !it->is_number())
            return nullptr;
        return Date(it->get<long long>());
    }

    // _id,updated_at vsetkych dokumentov (updated_at ako 2026-11-22T22:12:09.000Z)
    bool LoadUpdatedAt()
    {
        const string cmd = "mongoexport --config='" + config_ + "' --db=fpcstat --collection=" + Collection() +
                           " --fields=_id,updated_at --type=csv --noHeaderLine --quiet";
        FILE *in = popen(cmd.c_str(), "r");
        if (in == nullptr)
        {
            cout << "Mongo: mongoexport sa nepodarilo spustit - zapis do Mongo vypnuty" << endl;
            return false;
        }
        updated_at_.reserve(2500000);
        char buf[256];
        while (fgets(buf, sizeof(buf), in) != nullptr)
        {
            char *comma = strchr(buf, ',');
            if (comma == nullptr)
                continue;
            *comma = '\0';
            long long id = atoll(buf);
            struct tm tm_ = {};
            long long epoch = 0;
            if (strptime(comma + 1, "%Y-%m-%dT%H:%M:%S", &tm_) != nullptr)
                epoch = static_cast<long long>(timegm(&tm_));
            if (id > 0)
                updated_at_[id] = epoch;
        }
        int rc = pclose(in);
        if (rc != 0)
        {
            cout << "Mongo: mongoexport skoncil s chybou (rc=" << rc << ") - zapis do Mongo vypnuty" << endl;
            return false;
        }
        return true;
    }
};

// Statistiky jednej tabulky: damage_dealt, spotted, frags, dropped_capture_points,
// battles, wins, battle_avg_xp (v tomto poradi)
typedef array<long long, 7> StatArr;

static const int kTableCount = 5;
static const char *const kTables[kTableCount] = {
    "players_stat_all", "players_stat_defense", "players_stat_skirmish",
    "players_stat_gm_absolute", "players_stat_gm_champion"};
// Kluc v JSON "statistics" pre kazdu z tabuliek vyssie
static const char *const kJsonKeys[kTableCount] = {
    "all", "stronghold_defense", "stronghold_skirmish",
    "globalmap_absolute", "globalmap_champion"};

struct PlayerStats
{
    long long account_id = 0;
    StatArr stat[kTableCount];
    bool ok[kTableCount] = {false, false, false, false, false};
};

// Vyberie len udaje relevantne pre vypocet WN8 a avgXP per battle.
// Vracia false ak v JSON chyba blok alebo niektora hodnota - hracovi sa potom
// tabulka v tejto davke preskoci (nezapisuju sa nuly ani nahodne hodnoty).
static bool ExtractBattleStats(const nlohmann::json &player, StatArr &out, const char *table)
{
    static const char *const keys[7] = {"damage_dealt", "spotted", "frags", "dropped_capture_points",
                                        "battles", "wins", "battle_avg_xp"};
    out.fill(0);

    auto st = player.find("statistics");
    if (st == player.end() || !st->is_object())
    {
        cout << "Chyba blok statistics pre: " << table << endl;
        return false;
    }
    auto t = st->find(table);
    if (t == st->end() || !t->is_object())
    {
        cout << "Prazdna hodnota pre: " << table << endl;
        return false;
    }
    for (int i = 0; i < 7; i++)
    {
        auto it = t->find(keys[i]);
        if (it == t->end() || !it->is_number())
        {
            cout << "Chybajuca hodnota " << keys[i] << " pre: " << table << endl;
            return false;
        }
        out[i] = it->get<long long>();
    }
    return true;
}

// Zoznam cisel ako PostgreSQL pole: {1,2,3}
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

// 8 stlpcov: 7 statistik + account_id (v poradi ako v SQL nizsie)
typedef array<vector<long long>, 8> Columns;

static void PushRow(Columns &c, const StatArr &st, long long account_id)
{
    for (int i = 0; i < 7; i++)
        c[i].push_back(st[i]);
    c[7].push_back(account_id);
}

// Vykona davkovy prikaz, ktory dostane 8 poli ($1..$8) cez unnest
static void ExecColumns(PGconn *conn, const string &sql, const Columns &c, const string &what)
{
    array<string, 8> values;
    array<const char *, 8> params;
    for (int i = 0; i < 8; i++)
    {
        values[i] = PgArray(c[i]);
        params[i] = values[i].c_str();
    }
    PGresult *result = PQexecParams(conn, sql.c_str(), 8, nullptr, params.data(), nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
        cout << "Chyba " << what << ": " << PQresultErrorMessage(result) << endl;
    PQclear(result);
}

// Nacita aktualne riadky pre davku account_id jednym dotazom
static bool FetchExisting(PGconn *conn, const string &table, const vector<long long> &ids,
                          unordered_map<long long, StatArr> *existing)
{
    const string query = "SELECT account_id,damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp FROM " +
                         table + " WHERE account_id = ANY($1::bigint[])";
    const string arr = PgArray(ids);
    const char *params[1] = {arr.c_str()};

    PGresult *result = PQexecParams(conn, query.c_str(), 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        cout << "Chyba SELECT z " << table << ": " << PQresultErrorMessage(result) << endl;
        PQclear(result);
        return false;
    }

    int n = PQntuples(result);
    existing->reserve(n);
    for (int i = 0; i < n; i++)
    {
        StatArr st;
        for (int k = 0; k < 7; k++)
            st[k] = PQgetisnull(result, i, k + 1) ? 0 : stoll(PQgetvalue(result, i, k + 1));
        (*existing)[stoll(PQgetvalue(result, i, 0))] = st;
    }
    PQclear(result);
    return true;
}

// Zapise davku hracov do jednej tabulky: nove riadky INSERT, existujuce UPDATE
// a pri prirastku battles aj riadok do <tabulka>_history (rozdiel oproti starej hodnote).
static void WriteTable(PGconn *conn, int ti, const vector<PlayerStats> &players)
{
    const string table = kTables[ti];

    vector<long long> ids;
    ids.reserve(players.size());
    for (const auto &p : players)
        if (p.ok[ti])
            ids.push_back(p.account_id);
    if (ids.empty())
        return;

    unordered_map<long long, StatArr> existing;
    if (!FetchExisting(conn, table, ids, &existing))
        return; // bez znalosti existujucich riadkov nemozno rozhodnut insert/update

    Columns ins, upd, his;
    for (const auto &p : players)
    {
        if (!p.ok[ti])
            continue;
        const StatArr &now = p.stat[ti];
        auto it = existing.find(p.account_id);
        if (it == existing.end())
        {
            PushRow(ins, now, p.account_id);
            continue;
        }
        const StatArr &old = it->second;
        if (now[4] - old[4] > 0)
        {
            StatArr diff;
            for (int i = 0; i < 7; i++)
                diff[i] = now[i] - old[i];
            PushRow(his, diff, p.account_id);
        }
        PushRow(upd, now, p.account_id);
    }

    const string unnest = "unnest($1::bigint[],$2::bigint[],$3::bigint[],$4::bigint[],$5::bigint[],$6::bigint[],$7::bigint[],$8::bigint[]) AS v(d,s,f,c,b,w,x,id)";

    if (!ins[7].empty())
        ExecColumns(conn,
                    "INSERT INTO " + table + "(damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,account_id) "
                    "SELECT v.d,v.s,v.f,v.c,v.b,v.w,v.x,v.id FROM " + unnest,
                    ins, "insertu do " + table);

    if (!his[7].empty())
        ExecColumns(conn,
                    "INSERT INTO " + table + "_history (damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,date,account_id) "
                    "SELECT v.d,v.s,v.f,v.c,v.b,v.w,v.x, now() - interval '1 day', v.id FROM " + unnest +
                    " WHERE true ON CONFLICT DO NOTHING",
                    his, "insertu do " + table + "_history");

    if (!upd[7].empty())
        ExecColumns(conn,
                    "UPDATE " + table + " AS t SET damage_dealt=v.d,spotted=v.s,frags=v.f,dropped_capture_points=v.c,"
                    "battles=v.b,wins=v.w,battle_avg_xp=v.x FROM " + unnest + " WHERE t.account_id=v.id",
                    upd, "update " + table);
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
static int ProcessJson(const string &json_data, PGconn *conn, MongoSync *mongo, bool write_gr)
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

    // Ucet povazujeme za vymazany LEN ak server explicitne odpovie status=ok
    // a pre dany account_id vrati null. Ak zlyhala cela odpoved (timeout, chyba
    // API, ina struktura), nesmieme null v "data" zamienat za vymazany ucet.
    if (!js.contains("status") || js["status"] != "ok")
    {
        cout << "WoT API odpoved nema status=ok, davku preskakujem: " << json_data << endl;
        return 0;
    }

    if (!js.contains("data") || js["data"].is_null())
    {
        return 0;
    }

    const json &data = js.at("data");

    int dead_count = 0;
    vector<long long> gr_ids, gr_values; // global_rating pre players_stat_all
    vector<PlayerStats> players;
    players.reserve(data.size());

    PGresult *tx = PQexec(conn, "BEGIN");
    PQclear(tx);

    for (auto &x : data.items())
    {
        const json &j = x.value();

        if (j.is_null())
        {
            // Ucet bol vymazany na serveri - nema zmysel z neho citat statistiky
            InsertDeadPlayer(conn, x.key());
            dead_count++;
            continue;
        }

        PlayerStats p;
        try
        {
            p.account_id = stoll(x.key());
        }
        catch (const std::exception &)
        {
            cout << "Neplatne account_id v odpovedi: " << x.key() << endl;
            continue;
        }

        for (int ti = 0; ti < kTableCount; ti++)
            p.ok[ti] = ExtractBattleStats(j, p.stat[ti], kJsonKeys[ti]);

        if (mongo != nullptr)
            mongo->Write(p.account_id, j);

        if (write_gr)
        {
            auto gr = j.find("global_rating");
            if (gr != j.end() && gr->is_number())
            {
                gr_ids.push_back(p.account_id);
                gr_values.push_back(gr->get<long long>());
            }
        }

        players.push_back(std::move(p));
    }

    for (int ti = 0; ti < kTableCount; ti++)
        WriteTable(conn, ti, players);

    // global_rating az po WriteTable (novi hraci uz maju riadok v players_stat_all)
    if (!gr_ids.empty())
    {
        const string ids = PgArray(gr_ids), vals = PgArray(gr_values);
        const char *params[2] = {ids.c_str(), vals.c_str()};
        PGresult *r = PQexecParams(conn,
                                   "UPDATE players_stat_all t SET global_rating = v.g "
                                   "FROM unnest($1::bigint[], $2::bigint[]) AS v(id, g) "
                                   "WHERE t.account_id = v.id AND t.global_rating IS DISTINCT FROM v.g",
                                   2, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_COMMAND_OK)
            cout << "Chyba update global_rating: " << PQresultErrorMessage(r) << endl;
        PQclear(r);
    }

    PGresult *txc = PQexec(conn, "COMMIT");
    PQclear(txc);

    return dead_count;
}

// Vycisti databazu od zaznamov starsich ako 2 mesiace.
//
// Maze po davkach po 50000 riadkov (kazda davka je samostatna autocommit
// transakcia), aby sa nedrzali dlhe zamky ani obrovska transakcia. Davka ide cez
// primarny kluc (account_id, date), takze vyuzije index a nesiaha na cely stol.
//
// VACUUM tu zamerne nie je: v jednom PQexec s DELETE zlyhava (VACUUM nesmie bezat
// v multi-command stringu, cely retazec sa vratil spat) a o upratanie sa stara
// autovacuum.
static void CleanUpHistory()
{
    const long long kBatch = 50000;

    Pgsql db;
    PGconn *conn = db.Get();
    if (conn == nullptr)
    {
        cout << "CleanUpHistory: bez spojenia s databazou, preskakujem" << endl;
        return;
    }

    for (int ti = 0; ti < kTableCount; ti++)
    {
        const string h = string(kTables[ti]) + "_history";
        const string query =
            "WITH d AS ("
            "DELETE FROM " + h + " t USING ("
            "SELECT account_id, date FROM " + h +
            " WHERE date < (current_date - interval '2 months')::date LIMIT " + to_string(kBatch) +
            ") x WHERE t.account_id = x.account_id AND t.date = x.date RETURNING 1) "
            "SELECT count(*) FROM d";

        long long total = 0;
        for (;;)
        {
            PGresult *result = PQexec(conn, query.c_str());
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {
                cout << "Chyba CleanUpHistory pre " << h << ": " << PQresultErrorMessage(result) << endl;
                PQclear(result);
                break;
            }
            long long n = stoll(PQgetvalue(result, 0, 0));
            PQclear(result);

            total += n;
            if (n < kBatch)
                break; // posledna (neuplna) davka
        }

        if (total > 0)
            cout << "CleanUpHistory " << h << ": zmazanych " << total << endl;
    }
}

int RunJob(const JobConfig &config)
{
    CronLog::ScopedRun cron_log(config.cron_log_name);

    timestamp_t t0 = GetTimestamp();
    time_t start, stop;
    time(&start);
    cout << endl
         << "Program zacal pracovat: " << ctime(&start) << endl;

    AccountSource players(config.count_query, config.source_table);
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
    cout << "ZDROJ " << config.source_table << endl;

    container a_ids;
    SendCurl send; // jedno spojenie (keep-alive) pre cely beh

    // MongoDB fpcstat.player-stats (iba ak je zapnute v JobConfig a nie je PLAYERSSTAT_NOMONGO=1)
    MongoSync mongo_sync;
    const char *nomongo_env = std::getenv("PLAYERSSTAT_NOMONGO");
    const bool want_mongo = config.write_mongo && !(nomongo_env && *nomongo_env && *nomongo_env != '0');
    MongoSync *mongo = (want_mongo && mongo_sync.Open()) ? &mongo_sync : nullptr;

    int ntuples = 0;
    int spracovanych = 0;
    int kolo = 0;
    int mrtvi = 0;

    // Keyset kurzor - posledne account_id z predchadzajucej davky
    int last_account_id = 0;

    while (spracovanych < riadkov)
    {
        kolo++;
        players.GetPlayers(last_account_id, &a_ids, &ntuples);

        if (ntuples == 0)
            break;

        if (ntuples < 500)
            cout << "pocet hracov na spracovanie: " << ntuples << endl;

        // Dotaz vracia riadky zoradene podla account_id, posledny je novy kurzor
        last_account_id = a_ids.back();

        for (size_t start_idx = 0; start_idx < a_ids.size(); start_idx += 100)
        {
            size_t end_idx = start_idx + 100;
            string aids;
            string json_response;
            GetAccountId(start_idx, end_idx, a_ids, &aids);
            if (!aids.empty())
            {
                SendPost(send, aids, &json_response, want_mongo || config.write_global_rating);
                mrtvi += ProcessJson(json_response, players.conn, mongo, config.write_global_rating);
            }
        }

        spracovanych += ntuples;
        ntuples = 0;
    }

    if (mrtvi > 0)
    {
        cout << "Vymazanych uctov najdenych: " << mrtvi << endl;
    }

    string mongo_info = "off";
    if (mongo != nullptr)
    {
        const bool ok = mongo->Close();
        cout << "Mongo: zapisanych " << mongo->written << ", bez zmeny " << mongo->skipped
             << (ok ? "" : " - CHYBA importu") << endl;
        mongo_info = to_string(mongo->written) + "/" + to_string(mongo->skipped) + (ok ? "" : "/CHYBA");
    }
    else if (want_mongo)
    {
        mongo_info = "CHYBA";
    }

    // PLAYERSSTAT_NOCLEANUP=1 preskoci mazanie historie (na testovanie)
    const char *nocleanup_env = std::getenv("PLAYERSSTAT_NOCLEANUP");
    const bool skip_cleanup = nocleanup_env && *nocleanup_env && *nocleanup_env != '0';

    if (config.run_cleanup && !skip_cleanup)
    {
        CleanUpHistory();
    }

    timestamp_t t2 = GetTimestamp();
    double secs = (t2 - t0) / 1000000.0L;
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
    if (config.write_mongo)
        info << " Mongo=" << mongo_info;
    cron_log.SetInfo(info.str());

    return 0;
}

} // namespace PlayerStatProcessing
