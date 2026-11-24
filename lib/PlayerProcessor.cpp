#include <cstdlib>
#include "PlayerProcessor.h"
#include "SendCurl.h"
#include "PlayerNick.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

namespace PlayerProcessing
{

// Implementacia Statistika
void Statistika::Print() const
{
    std::cout << "Spracovane klany \t" << clans_processed << std::endl;
    std::cout << "Kontrolovani hraci \t" << players_checked << std::endl;
    std::cout << "Prichody do klanu \t" << new_players_all << std::endl;
    std::cout << "Odchody z klanu \t" << players_left << std::endl;
    std::cout << "Uplne nove ID \t\t" << new_account_id << std::endl;
    std::cout << "Zmeny nickov \t\t" << change_nick << std::endl;
    std::cout << "Zmeny roly \t\t" << change_role << std::endl;
    std::cout << "Ukoncene klany \t\t" << empty_clan << std::endl;
    std::cout << "Rozpustene (uzavrete)\t" << empty_clans_closed << " klanov, " << empty_rows_closed << " zaznamov" << std::endl;
    std::cout << "Odznacene zive klany\t" << empty_clans_unmarked << std::endl;
    std::cout << "Preskocene klany (API)\t" << skipped_clans << std::endl;
}

void Statistika::Reset()
{
    new_players_all = 0;
    new_account_id = 0;
    change_nick = 0;
    repair_members_role = 0;
    new_members_role = 0;
    change_role = 0;
    empty_clan = 0;
    players_left = 0;
    clans_processed = 0;
    players_checked = 0;
    empty_clans_closed = 0;
    empty_rows_closed = 0;
    empty_clans_unmarked = 0;
    skipped_clans = 0;
}

// Pomocne funkcie
void GetClanId(ClanQueue* p_aids, PGconn* permanent_connection, const std::string& query)
{
    PGresult* result = PQexec(permanent_connection, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cout << "Chyba select clan_all" << PQresultErrorMessage(result) << std::endl;
    }

    int riadkov = PQntuples(result);

    for (int i = 0; i < riadkov; i++) {
        p_aids->push(std::stoi(PQgetvalue(result, i, 0)));
    }

    PQclear(result);
}

void Get100Id(std::string* p_clan_id, ClanQueue* p_aids, int a_clan_id[100])
{
    int i = 0;
    while (!p_aids->empty()) {
        int c = p_aids->front();
        *p_clan_id += std::to_string(c) + ",";
        a_clan_id[i] = p_aids->front();
        p_aids->pop();
        i++;
        if (i == 100) {
            break;
        }
    }
}

bool SendPost(const std::string& clan_id, std::string* json, int max_attempts)
{
    const char text[] = "&fields=clan_id,members_count,members.account_id,members.account_name,members.joined_at,members.role_i18n&language=cs";
    const std::string method = "/clans/info/";
    std::string post_data = std::string(text) + "&clan_id=" + clan_id;

    SendCurl send;
    std::chrono::seconds dura(3);

    for (int attempt = 1; max_attempts <= 0 || attempt <= max_attempts; ++attempt) {
        try {
            *json = send.SendWGN(method, post_data);
            return true;
        }
        catch (std::exception& e) {
            std::cout << e.what() << std::endl;
            std::this_thread::sleep_for(dura);
        }
    }
    return false;
}

void SendPostBatch(std::string* clan_id, int a_clan_id[100], std::string* json, Statistika& stat)
{
    // WG pri niektorych klanoch trvalo vracia chybu (napr. 504 SOURCE_NOT_AVAILABLE pre konkretny
    // clan_id) a cela davka 100 klanov potom zlyhava. Predtym sa davka opakovala donekonecna a beh
    // ALL sa zasekol. Teraz sa po 5 pokusoch davka rozlozi po jednom klane; klany, ktore zlyhaju aj
    // samostatne, sa v tomto behu preskocia (neoznacia sa ako prazdne - clenstvo sa nemeni).
    // Ak zlyha kazdy klan, je nedostupne cele API - pocka sa a skusa sa dalej.
    while (!SendPost(*clan_id, json, 5)) {
        std::vector<int> good, bad;
        std::string tmp;
        for (int i = 0; i < 100 && a_clan_id[i] != 0; ++i) {
            tmp.clear();
            (SendPost(std::to_string(a_clan_id[i]), &tmp, 2) ? good : bad).push_back(a_clan_id[i]);
        }
        if (good.empty()) {
            std::cout << "WG API nedostupne pre celu davku, cakam 60 s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue;
        }
        for (int cid : bad) {
            std::cout << "Preskakujem klan " << cid << " (WG API vracia chybu)" << std::endl;
            stat.skipped_clans++;
        }
        ClearArray(a_clan_id);
        clan_id->clear();
        for (size_t i = 0; i < good.size(); ++i) {
            a_clan_id[i] = good[i];
            *clan_id += std::to_string(good[i]) + ",";
        }
    }
}

namespace
{
// int pole ako PostgreSQL literal {1,2,3}
std::string IntArrayLiteral(const std::vector<int>& ids)
{
    std::ostringstream out;
    out << '{';
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) out << ',';
        out << ids[i];
    }
    out << '}';
    return out.str();
}

int ExecWithIntArray(PGconn* conn, const char* query, const std::vector<int>& ids)
{
    std::string arr = IntArrayLiteral(ids);
    const char* params[1] = { arr.c_str() };
    PGresult* res = PQexecParams(conn, query, 1, nullptr, params, nullptr, nullptr, 0);
    int affected = 0;
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba SkontrolujPrazdneKlany: " << PQresultErrorMessage(res) << std::endl;
        affected = -1;
    } else {
        affected = std::atoi(PQcmdTuples(res));
    }
    PQclear(res);
    return affected;
}
} // namespace

void SkontrolujPrazdneKlany(PGconn* conn, Statistika& stat)
{
    // clan_all_empty plnia Players (clan_is_empty) aj KontrolaKlanov (rozpustene / chybajuce v
    // clans/list). Klan v nej sa uz nespracuva, takze bez tohto kroku by otvorene zaznamy jeho
    // clenov ostali otvorene navzdy a odchody z rozpusteneho klanu by sa nikdy neukazali.
    PGresult* res = PQexec(conn,
        "SELECT DISTINCT m.clan_id FROM members_role m JOIN clan_all_empty e USING (clan_id)"
        " WHERE m.dokedy IS NULL ORDER BY 1");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba SELECT clan_all_empty: " << PQresultErrorMessage(res) << std::endl;
        PQclear(res);
        return;
    }
    std::vector<int> ids;
    for (int i = 0; i < PQntuples(res); ++i) {
        ids.push_back(std::atoi(PQgetvalue(res, i, 0)));
    }
    PQclear(res);
    if (ids.empty()) return;

    std::vector<int> dead, alive;
    SendCurl send;
    for (size_t i = 0; i < ids.size(); i += 100) {
        std::ostringstream list;
        for (size_t j = i; j < ids.size() && j < i + 100; ++j) {
            if (j > i) list << ',';
            list << ids[j];
        }
        std::string post = "&fields=clan_id,members_count,is_clan_disbanded&clan_id=" + list.str();
        nlohmann::json js;
        try {
            js = nlohmann::json::parse(send.SendWGN("/clans/info/", post));
        }
        catch (const std::exception& e) {
            // Pri chybe API davku nemenit - skusi sa pri dalsom behu
            std::cerr << "SkontrolujPrazdneKlany: chyba API, preskakujem davku: " << e.what() << std::endl;
            continue;
        }
        if (js.value("status", "") != "ok" || !js.contains("data")) {
            std::cerr << "SkontrolujPrazdneKlany: neplatna odpoved API, preskakujem davku" << std::endl;
            continue;
        }
        for (size_t j = i; j < ids.size() && j < i + 100; ++j) {
            std::string key = std::to_string(ids[j]);
            if (!js["data"].contains(key)) continue;   // nevieme -> nemenit
            const auto& c = js["data"][key];
            bool is_dead = c.is_null()
                || c.value("is_clan_disbanded", false)
                || !c.contains("members_count") || c["members_count"].is_null()
                || c["members_count"].get<int>() == 0;
            (is_dead ? dead : alive).push_back(ids[j]);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!dead.empty()) {
        int rows = ExecWithIntArray(conn,
            "UPDATE members_role SET dokedy = GREATEST(odkedy, current_date)"
            " WHERE clan_id = ANY($1::int[]) AND dokedy IS NULL", dead);
        ExecWithIntArray(conn, "UPDATE players_all SET clan_id = 0 WHERE clan_id = ANY($1::int[])", dead);
        if (rows > 0) {
            stat.empty_clans_closed += static_cast<int>(dead.size());
            stat.empty_rows_closed += rows;
        }
    }
    if (!alive.empty()) {
        int rows = ExecWithIntArray(conn, "DELETE FROM clan_all_empty WHERE clan_id = ANY($1::int[])", alive);
        if (rows > 0) stat.empty_clans_unmarked += rows;
    }
    std::cout << "Prazdne klany s otvorenymi zaznamami: " << ids.size()
              << " (rozpustene " << dead.size() << ", zive " << alive.size() << ")" << std::endl;
}

void ClearArray(int a_clan_id[100])
{
    for (int i = 0; i < 100; i++) {
        a_clan_id[i] = 0;
    }
}

void PreparedSelectPlayersAll(PGconn* conn)
{
    const char* query = "SELECT account_id,nickname FROM players_all WHERE clan_id = $1 ";
    PGresult* result = PQprepare(conn, "players_all", query, 1, nullptr);

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Prepared statement players_all je chybny: " << PQerrorMessage(conn) << std::endl;
    }

    PQclear(result);
}

void PreparedSelectMembersRole(PGconn* conn)
{
    const char* query = "SELECT account_id,role,odkedy FROM members_role WHERE clan_id = $1 AND dokedy IS NULL";
    PGresult* result = PQprepare(conn, "members_role", query, 1, nullptr);

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Prepared statement members_role je chybny: " << PQerrorMessage(conn) << std::endl;
    }

    PQclear(result);
}

// Implementacia triedy Spracuj
Spracuj::Spracuj(int clan_id[100], const std::string& json, PGconn* conn, Statistika& s)
    : permanent_connection(conn), stat(s)
{
    int i = 0, cid, members_count;
    int* p_members_count = &members_count;
    std::string clan_data;
    std::string empty_reason;

    while (clan_id[i] != 0) {
        cid = clan_id[i];

        // Kontajnery musia byt prazdne pre kazdy klan. Predtym sa pri prazdnom klane nevycistil
        // members_role, takze otvorene zaznamy predosleho klanu sa porovnali s dalsim klanom
        // a nespravne sa ukoncili (a pri dalsom behu znova vlozili -> duplicity).
        players_all.clear();
        fresh.clear();
        members_role.clear();
        clan_data.clear();

        // Natiahne do containeru players_all udaje z databazy
        exec_select_players_all(cid);

        // Natiahne do containeru members_role udaje z databazy
        exec_select_members_role(cid);

        // Vyberiem z json iba clanove udaje
        empty_reason.clear();
        clan_data = spracuj_json_vyber_clan_id(json, cid, p_members_count, &empty_reason);

        // Ak je clan prazdny
        if (clan_data.empty()) {
            clan_is_empty(cid, empty_reason);
            stat.empty_clan++;
            update_players_all_empty(cid);

            // Statistika - spracovany klan aj ked je prazdny
            stat.clans_processed++;

            players_all.clear();
            fresh.clear();
            i++;
            continue;
        }

        // Ak je iny pocet clenov v tabulke clan_all oprav to
        if (members_count != static_cast<int>(players_all.size())) {
            update_clan_all_members_count(cid, members_count);
            // std::cout << "Update members_count" << std::endl;
        }

        // Naplni container fresh udajmi z wargamingu
        spracuj_json_map(clan_data, p_members_count);

        spracuj_players_all(cid, players_all, fresh);
        spracuj_members_role(cid, members_role, fresh);

        // Statistika - spracovany klan a kontrolovani hraci
        stat.clans_processed++;
        stat.players_checked += players_all.size();

        i++;
        if (i >= 100) {
            break;
        }

        players_all.clear();
        fresh.clear();
        members_role.clear();
        clan_data.clear();
    }
}

Spracuj::~Spracuj()
{
}

void Spracuj::spracuj_players_all(int clan_id, const PlayerMap& players_all_map, const PlayerMap& fresh_map)
{
    // Hladam kto nie je v klane
    for (auto it = players_all_map.begin(); it != players_all_map.end(); ++it) {
        if (fresh_map.count(it->first) == 0) {
            nie_je_v_klane_players_all(it->first);
        }
    }

    // Hladam kto je novy v klane
    for (auto it = fresh_map.begin(); it != fresh_map.end(); ++it) {
        if (players_all_map.count(it->first) == 0) {
            const Player& fr = it->second;
            novy_v_klane_players_all(fr.account_id, clan_id, fr.account_name);
        }
    }

    // Zmenil si nick?
    for (auto it = fresh_map.begin(); it != fresh_map.end(); ++it) {
        const Player& fr = it->second;
        auto it2 = players_all_map.find(fr.account_id);

        if (it2 != players_all_map.end()) {
            const Player& pl = it2->second;

            if (fr.account_name != pl.account_name) {
                zmena_nick_players_all(pl.account_name, fr.account_name, fr.account_id);
            }
        }
    }
}

void Spracuj::spracuj_members_role(int clan_id, const PlayerMap& members_role_map, const PlayerMap& fresh_map)
{
    // Hladam kto nie je v klane
    for (auto it = members_role_map.begin(); it != members_role_map.end(); ++it) {
        if (fresh_map.count(it->first) == 0) {
            nie_je_v_klane_members_role(it->first);
        }
    }

    // Hladam kto je novy v klane, alebo komu sa zmenila rola
    for (auto it = fresh_map.begin(); it != fresh_map.end(); ++it) {
        const Player& fr = it->second;
        auto mr = members_role_map.find(it->first);

        if (mr == members_role_map.end()) {
            novy_v_klane_members_role(fr.account_id, clan_id, fr.role_i18n, fr.joined_at);
        } else if (!fr.role_i18n.empty() && mr->second.role_i18n != fr.role_i18n) {
            zmena_roly_members_role(fr.account_id, clan_id, fr.role_i18n);
        }
    }
}

std::string Spracuj::spracuj_json_vyber_clan_id(const std::string& json, int clan_id, int* members_count, std::string* empty_reason)
{
    std::string clan_data;
    std::string clan_id_str = std::to_string(clan_id);

    // Hladame pattern: "clan_id": v JSON
    std::string search_pattern = "\"" + clan_id_str + "\":";
    size_t begin = json.find(search_pattern);

    if (begin == std::string::npos) {
        *members_count = 0;
        *empty_reason = "Clan_id nenajdeny v JSON odpovedi (klan pravdepodobne zruseny na serveri)";
        return "";
    }

    // Posunieme sa za "clan_id":
    size_t value_start = begin + search_pattern.length();

    // Preskocime whitespace
    while (value_start < json.length() && (json[value_start] == ' ' || json[value_start] == '\t' || json[value_start] == '\n')) {
        value_start++;
    }

    // Kontrola ci je hodnota null (klan bol zruseny)
    if (json.substr(value_start, 4) == "null") {
        *members_count = 0;
        *empty_reason = "Clan vratil null hodnotu (klan bol zruseny na serveri)";
        return "";
    }

    // Teraz hladame koniec dat klanu - moze byt "]}," alebo "]}" (ak je posledny)
    size_t end = json.find("]}}", value_start);
    size_t end_with_comma = json.find("]},", value_start);

    // Vyberieme blizsiu poziciu (moze byt len jeden z nich alebo oba)
    if (end_with_comma != std::string::npos && (end == std::string::npos || end_with_comma < end)) {
        end = end_with_comma;
    }

    if (end == std::string::npos) {
        *members_count = 0;
        *empty_reason = "Chyba pri parsovani JSON - nenajdeny koniec dat klanu";
        return "";
    }

    // +2 pre "]}" alebo +3 pre "]},"
    size_t end_length = (json[end + 2] == ',') ? 3 : 2;
    clan_data = json.substr(begin, (end + end_length) - begin);

    // Zistim kolko zaznamov ma klan
    begin = clan_data.find("members_count");
    end = clan_data.find(",", begin);

    try {
        *members_count = std::stoi(clan_data.substr(begin + 15, (end - begin + 15)));
    }
    catch (std::exception& e) {
        std::cout << "Chyba json vyber klan id: " << e.what() << std::endl;
        std::cout << clan_data << std::endl;
        *members_count = 0;
        *empty_reason = "Chyba pri parsovani members_count z JSON";
        clan_data.clear();
        return clan_data;
    }

    if (*members_count == 0) {
        *empty_reason = "Members_count = 0 (klan existuje ale nema clenov)";
        clan_data.clear();
    }

    return clan_data;
}

void Spracuj::spracuj_json_map(const std::string& clan_data_input, int* /*members_count*/)
{
    std::string clan_data = clan_data_input;
    size_t begin = clan_data.find("[");
    size_t end = clan_data.find("]", begin);

    if (begin == std::string::npos || end == std::string::npos) {
        return;
    }

    clan_data = clan_data.substr(begin, end - begin);
    size_t posled = 0;

    while (true) {
        Player data;

        begin = clan_data.find("role_i18n", posled);
        if (begin == std::string::npos) break;

        end = clan_data.find(",", begin);
        data.role_i18n = clan_data.substr(begin + 12, (end - 1) - (begin + 12));

        begin = clan_data.find("joined_at", end);
        end = clan_data.find(",", begin);
        try {
            // Cislo (nie string v uvodzovkach) - stoll precita cislice a zastavi sa na ',' alebo '}'.
            // Povodne (end - 1) odrezalo poslednu cislicu (1423000000 -> 142300000 = rok 1974).
            long long joined = std::stoll(clan_data.substr(begin + 11, end - (begin + 11)));
            // Rozumny rozsah: 2010-01-01 .. 2038-01-19 (int), inak 0 (= dnesny datum)
            data.joined_at = (joined >= 1262304000LL && joined <= 2147483647LL) ? static_cast<int>(joined) : 0;
            if (data.joined_at == 0) {
                std::cout << "JSON MAP joined_at mimo rozsahu: " << joined << std::endl;
            }
        }
        catch (std::exception& e) {
            std::cout << "JSON MAP joined_at error" << std::endl;
            data.joined_at = 0;
        }

        begin = clan_data.find("account_name", end);
        end = clan_data.find(",", begin);
        data.account_name = clan_data.substr(begin + 15, (end - 1) - (begin + 15));

        begin = clan_data.find("account_id", end);
        end = clan_data.find(",", begin);
        try {
            data.account_id = std::stoi(clan_data.substr(begin + 12, (end - 1) - (begin + 12)));
        }
        catch (std::exception& e) {
            std::cout << "JSON MAP account_id error" << std::endl;
            data.account_id = 0;
        }

        posled = end;
        fresh[data.account_id] = data;
    }
}

// Databazove operacie
void Spracuj::novy_v_klane_members_role(int account_id, int clan_id, const std::string& role, int joined_at)
{
    // odkedy = datum vstupu do klanu podla WG (members.joined_at, unix epoch); ak chyba, dnesny datum.
    // Jeden prikaz:
    //  1. cur = posledny zaznam hraca v tomto klane od datumu vstupu (= stale to iste clenstvo,
    //     napr. po zmene roly); ak existuje, hrac bol nespravne ukonceny -> znovu ho otvori
    //     (rolu nemeni, pripadnu zmenu roly zachyti dalsi beh ako novy zaznam),
    //  2. ostatne otvorene zaznamy hraca ukonci k datumu vstupu (nie skor ako ich odkedy),
    //  3. ak cur neexistuje, vlozi nove clenstvo s odkedy = joined_at.
    static const char* query =
        "WITH p AS ("
        "  SELECT $1::int AS account_id, $2::int AS clan_id, $3::text AS role,"
        "         CASE WHEN $4::bigint > 0 THEN to_timestamp($4::bigint)::date ELSE current_date END AS odkedy"
        "), cur AS ("
        "  SELECT m.odkedy FROM members_role m, p"
        "  WHERE m.account_id = p.account_id AND m.clan_id = p.clan_id AND m.odkedy >= p.odkedy"
        "  ORDER BY m.odkedy DESC LIMIT 1"
        "), closed AS ("
        "  UPDATE members_role m SET dokedy = GREATEST(m.odkedy, p.odkedy) FROM p"
        "  WHERE m.account_id = p.account_id AND m.dokedy IS NULL"
        "    AND m.odkedy IS DISTINCT FROM (SELECT odkedy FROM cur)"
        "  RETURNING 1"
        "), reopened AS ("
        "  UPDATE members_role m SET dokedy = NULL FROM p, cur"
        "  WHERE m.account_id = p.account_id AND m.odkedy = cur.odkedy"
        "  RETURNING 1"
        ")"
        " INSERT INTO members_role (account_id, clan_id, role, odkedy)"
        " SELECT account_id, clan_id, role, odkedy FROM p"
        " WHERE NOT EXISTS (SELECT 1 FROM cur)";

    std::string a_id = std::to_string(account_id);
    std::string c_id = std::to_string(clan_id);
    std::string j_at = std::to_string(joined_at);
    const char* paramValues[4] = {a_id.c_str(), c_id.c_str(), role.c_str(), j_at.c_str()};

    PGresult* result = PQexecParams(permanent_connection, query, 4, nullptr, paramValues, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba insert members_role " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
}

void Spracuj::zmena_roly_members_role(int account_id, int clan_id, const std::string& role)
{
    // Zmena roly v tom istom klane: WG neposkytuje datum zmeny, preto odkedy = dnes.
    //  - otvoreny zaznam zacaty dnes (vstup alebo zmena roly dnes) -> iba prepise rolu,
    //  - inak ukonci otvoreny zaznam dnesnym dnom a vlozi novy s novou rolou.
    static const char* query =
        "WITH p AS (SELECT $1::int AS account_id, $2::int AS clan_id, $3::text AS role"
        "), same_day AS ("
        "  UPDATE members_role m SET role = p.role FROM p"
        "  WHERE m.account_id = p.account_id AND m.clan_id = p.clan_id AND m.dokedy IS NULL"
        "    AND m.odkedy >= current_date"
        "  RETURNING 1"
        "), closed AS ("
        "  UPDATE members_role m SET dokedy = current_date FROM p"
        "  WHERE m.account_id = p.account_id AND m.clan_id = p.clan_id AND m.dokedy IS NULL"
        "    AND m.odkedy < current_date"
        "  RETURNING 1"
        ")"
        " INSERT INTO members_role (account_id, clan_id, role, odkedy)"
        " SELECT account_id, clan_id, role, current_date FROM p"
        " WHERE NOT EXISTS (SELECT 1 FROM same_day) AND EXISTS (SELECT 1 FROM closed)";

    // ROLE_SYNC_INPLACE=1: jednorazova synchronizacia rol bez historie (skutocny datum zmeny
    // nepozname) - iba prepise rolu v otvorenom zazname. Pouzite 2026-11-24 pri zavedeni
    // sledovania rol, ked mali stare zaznamy stale rolu z prichodu.
    static const char* query_inplace =
        "UPDATE members_role SET role = $3"
        " WHERE account_id = $1::int AND clan_id = $2::int AND dokedy IS NULL";
    static const bool inplace = [] {
        const char* v = std::getenv("ROLE_SYNC_INPLACE");
        return v != nullptr && std::string(v) == "1";
    }();

    std::string a_id = std::to_string(account_id);
    std::string c_id = std::to_string(clan_id);
    const char* paramValues[3] = {a_id.c_str(), c_id.c_str(), role.c_str()};

    PGresult* result = PQexecParams(permanent_connection, inplace ? query_inplace : query, 3,
                                    nullptr, paramValues, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba zmena roly members_role " << PQresultErrorMessage(result) << std::endl;
    } else {
        stat.change_role++;
    }
    PQclear(result);
}

void Spracuj::nie_je_v_klane_members_role(int account_id)
{
    std::string query = "UPDATE members_role SET dokedy = now() WHERE dokedy IS NULL AND account_id = " + std::to_string(account_id);
    PGresult* result = PQexec(permanent_connection, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba UPDATE now members_role " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
}

void Spracuj::zmena_nick_players_all(const std::string& old_name, const std::string& new_name, int account_id)
{
    // Uvolni nick u zastaranych uctov, zapise historiu a zmeni nick v jednej transakcii
    if (!PlayerNick::Rename(permanent_connection, account_id, old_name, new_name)) {
        return; // chyba je uz vypisana; zmena sa skusi znova pri dalsom behu
    }

    stat.change_nick++;
}

void Spracuj::nie_je_v_klane_players_all(int account_id)
{
    std::string query = "UPDATE players_all SET clan_id = 0 WHERE account_id = " + std::to_string(account_id);
    PGresult* result = PQexec(permanent_connection, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba UPDATE 0 players_all " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);
    stat.players_left++;
}

void Spracuj::novy_v_klane_players_all(int account_id, int clan_id, const std::string& account_name)
{
    PGresult* result;

    std::string q1 = "SELECT account_id FROM players_all WHERE account_id = " + std::to_string(account_id);
    result = PQexec(permanent_connection, q1.c_str());

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cout << "Chyba select players_all " << PQresultErrorMessage(result) << std::endl;
    }

    int rows = PQntuples(result);
    PQclear(result);

    if (rows == 0) {
        // Nick sa najprv uvolni u zastaranych uctov (UNIQUE na players_all.nickname)
        if (PlayerNick::InsertPlayer(permanent_connection, account_id, clan_id, account_name)) {
            stat.new_account_id++;
        }
    }
    else {
        std::string u1 = "UPDATE players_all SET clan_id = " + std::to_string(clan_id) + " WHERE account_id = " + std::to_string(account_id);
        result = PQexec(permanent_connection, u1.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::cout << "Chyba update players_all " << PQresultErrorMessage(result) << std::endl;
        }
        PQclear(result);
    }

    stat.new_players_all++;
}

void Spracuj::clan_is_empty(int cid, const std::string& reason)
{
    PGresult* result;

    // Skontroluj ci clan_id uz nie je v tabulke
    std::string check_query = "SELECT clan_id FROM clan_all_empty WHERE clan_id = " + std::to_string(cid);
    result = PQexec(permanent_connection, check_query.c_str());

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba pri kontrole clan_all_empty: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return;
    }

    int rows = PQntuples(result);
    PQclear(result);

    if (rows > 0) {
        // Clan uz je v tabulke, len vypisem info
        std::cout << "Clan " << cid << " uz je v clan_all_empty. Dovod: " << reason << std::endl;
        return;
    }

    // Vloz novy zaznam
    std::string insert_query = "INSERT INTO clan_all_empty VALUES (" + std::to_string(cid) + ")";
    result = PQexec(permanent_connection, insert_query.c_str());

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba INSERT clan_all_empty pre clan " << cid << ": " << PQresultErrorMessage(result) << std::endl;
    } else {
        std::cout << "Clan " << cid << " oznaceny ako prazdny. Dovod: " << reason << std::endl;
    }

    PQclear(result);
}

void Spracuj::update_players_all_empty(int cid)
{
    std::string query = "UPDATE players_all SET clan_id = 0 WHERE clan_id = " + std::to_string(cid);
    PQexec(permanent_connection, query.c_str());
}

void Spracuj::update_clan_all_members_count(int cid, int members_count)
{
    std::string query = "UPDATE clan_all SET members_count = " + std::to_string(members_count) + " WHERE clan_id = " + std::to_string(cid);
    PQexec(permanent_connection, query.c_str());
}

void Spracuj::exec_select_players_all(int clan_id)
{
    PGresult* result;
    const char* paramValues[1];

    std::string a_id = std::to_string(clan_id);
    paramValues[0] = a_id.c_str();

    result = PQexecPrepared(permanent_connection, "players_all", 1, paramValues, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Select statement players_all je chybny: " << PQresultErrorMessage(result) << std::endl;
    }

    int riadkov = PQntuples(result);

    for (int i = 0; i < riadkov; i++) {
        Player data;
        data.account_name = PQgetvalue(result, i, 1);
        data.account_id = std::stoi(PQgetvalue(result, i, 0));
        players_all[data.account_id] = data;
    }

    PQclear(result);
}

void Spracuj::exec_select_members_role(int clan_id)
{
    PGresult* result;
    const char* paramValues[1];

    std::string a_id = std::to_string(clan_id);
    paramValues[0] = a_id.c_str();

    result = PQexecPrepared(permanent_connection, "members_role", 1, paramValues, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Select statement members_role je chybny: " << PQresultErrorMessage(result) << std::endl;
    }

    int riadkov = PQntuples(result);

    for (int i = 0; i < riadkov; i++) {
        Player data;
        data.role_i18n = PQgetvalue(result, i, 1);
        data.account_id = std::stoi(PQgetvalue(result, i, 0));
        data.joined_at = std::stoi(PQgetvalue(result, i, 2));
        members_role[data.account_id] = data;
    }

    PQclear(result);
}

} // namespace PlayerProcessing
