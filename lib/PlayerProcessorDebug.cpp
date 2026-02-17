#include "PlayerProcessorDebug.h"
#include "SendCurl.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace PlayerProcessingDebug
{

// Implementacia Statistika
void Statistika::Print() const
{
    std::cout << "Prichody do klanu \t" << new_players_all << std::endl;
    std::cout << "Odchody z klanu \t" << players_left << std::endl;
    std::cout << "Uplne nove ID \t\t" << new_account_id << std::endl;
    std::cout << "Zmeny nickov \t\t" << change_nick << std::endl;
    std::cout << "Ukoncene klany \t\t" << empty_clan << std::endl;
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

void SendPost(const std::string& clan_id, std::string* json)
{
    const char text[] = "&fields=clan_id,members_count,members.account_id,members.account_name,members.joined_at,members.role_i18n&language=cs";
    const std::string method = "/clans/info/";
    std::string post_data = std::string(text) + "&clan_id=" + clan_id;

    SendCurl send;
    int x = 1;
    std::chrono::seconds dura(3);

    do {
        try {
            *json = send.SendWGN(method, post_data);
            x = 0;
        }
        catch (std::exception& e) {
            std::cout << e.what() << std::endl;
            std::this_thread::sleep_for(dura);
            x = 1;
        }
    } while (x != 0);
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

    // Hladam kto je novy v klane
    for (auto it = fresh_map.begin(); it != fresh_map.end(); ++it) {
        if (members_role_map.count(it->first) == 0) {
            const Player& fr = it->second;
            novy_v_klane_members_role(fr.account_id, clan_id, fr.role_i18n);
        }
    }
}

// ============================================================================
// DEBUG VERZIA - Vypise JSON a zastavi program ked zisti prazdny klan
// ============================================================================
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

        // DEBUG: Vypis JSON a zastav program
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "DEBUG: KLAN NEEXISTUJE - CLAN_ID: " << clan_id << std::endl;
        std::cout << "==================================================================" << std::endl;
        std::cout << "Dovod: " << *empty_reason << std::endl;
        std::cout << "Hladany pattern: " << search_pattern << std::endl;
        std::cout << "\nCely JSON odpoved zo servera:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << json << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nProgram sa zastavuje pre inspekciu..." << std::endl;
        std::cout << "==================================================================" << std::endl;
        exit(1);
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

        // DEBUG: Vypis JSON a zastav program
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "DEBUG: KLAN VRATIL NULL - CLAN_ID: " << clan_id << std::endl;
        std::cout << "==================================================================" << std::endl;
        std::cout << "Dovod: " << *empty_reason << std::endl;
        std::cout << "Najdeny pattern: \"" << clan_id_str << "\": null" << std::endl;
        std::cout << "\nCely JSON odpoved zo servera:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << json << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nProgram sa zastavuje pre inspekciu..." << std::endl;
        std::cout << "==================================================================" << std::endl;
        exit(1);
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
        *empty_reason = "Chyba pri parsovani JSON - nenajdeny koniec dat klanu (hladal som ]}}, alebo ]},)";

        // DEBUG: Vypis JSON a zastav program
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "DEBUG: CHYBA PARSOVANIA - CLAN_ID: " << clan_id << std::endl;
        std::cout << "==================================================================" << std::endl;
        std::cout << "Dovod: " << *empty_reason << std::endl;
        std::cout << "\nCast JSON okolo clan_id (500 znakov od value_start):" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        if (value_start + 500 < json.length()) {
            std::cout << json.substr(value_start, 500) << "..." << std::endl;
        } else {
            std::cout << json.substr(value_start) << std::endl;
        }
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nCely JSON odpoved zo servera:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << json << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nProgram sa zastavuje pre inspekciu..." << std::endl;
        std::cout << "==================================================================" << std::endl;
        exit(1);
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

        // DEBUG: Vypis JSON a zastav program
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "DEBUG: CHYBA PRI PARSOVANI - CLAN_ID: " << clan_id << std::endl;
        std::cout << "==================================================================" << std::endl;
        std::cout << "Dovod: " << *empty_reason << std::endl;
        std::cout << "Exception: " << e.what() << std::endl;
        std::cout << "\nCely JSON odpoved zo servera:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << json << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nProgram sa zastavuje pre inspekciu..." << std::endl;
        std::cout << "==================================================================" << std::endl;
        exit(1);
    }

    if (*members_count == 0) {
        *empty_reason = "Members_count = 0 (klan existuje ale nema clenov)";

        // DEBUG: Vypis JSON a zastav program
        std::cout << "\n==================================================================" << std::endl;
        std::cout << "DEBUG: KLAN MA 0 CLENOV - CLAN_ID: " << clan_id << std::endl;
        std::cout << "==================================================================" << std::endl;
        std::cout << "Dovod: " << *empty_reason << std::endl;
        std::cout << "\nData konkretneho klanu:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << clan_data << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nCely JSON odpoved zo servera:" << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << json << std::endl;
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "\nProgram sa zastavuje pre inspekciu..." << std::endl;
        std::cout << "==================================================================" << std::endl;
        exit(1);
    }

    return clan_data;
}

void Spracuj::spracuj_json_map(const std::string& clan_data_input, int* members_count)
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
            data.joined_at = std::stoi(clan_data.substr(begin + 11, (end - 1) - (begin + 11)));
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
void Spracuj::novy_v_klane_members_role(int account_id, int clan_id, const std::string& role)
{
    PGresult* result;

    // Skontroluj ci ma nieco neukoncene
    std::string q1 = "SELECT * FROM members_role WHERE account_id = " + std::to_string(account_id) + " AND dokedy IS NULL";

    result = PQexec(permanent_connection, q1.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cout << "Chyba select members_role " << PQresultErrorMessage(result) << std::endl;
    }

    int rows = PQntuples(result);
    PQclear(result);

    if (rows >= 1) {
        std::string q2 = "UPDATE members_role SET dokedy = now() WHERE dokedy IS NULL AND account_id = " + std::to_string(account_id);
        result = PQexec(permanent_connection, q2.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::cout << "Chyba update members_role stare dokedy " << PQresultErrorMessage(result) << std::endl;
        }
        PQclear(result);
    }

    // Vlozim nove udaje
    std::string q3 = "INSERT INTO members_role (account_id,clan_id,role,odkedy) VALUES (" +
                     std::to_string(account_id) + "," + std::to_string(clan_id) + ",'" + role + "', now())";
    result = PQexec(permanent_connection, q3.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba insert members_role " << PQresultErrorMessage(result) << std::endl;
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
    PGresult* result;

    std::string q1 = "INSERT INTO players_nick (account_id,nick) VALUES (" + std::to_string(account_id) + ",'" + old_name + "')";
    result = PQexec(permanent_connection, q1.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba INSERT players_nick " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);

    std::string q2 = "UPDATE players_all SET nickname = '" + new_name + "' WHERE account_id = " + std::to_string(account_id);
    result = PQexec(permanent_connection, q2.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cout << "Chyba UPDATE nick in players_all " << PQresultErrorMessage(result) << std::endl;
    }
    PQclear(result);

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
        std::string i1 = "INSERT INTO players_all (account_id,clan_id,nickname) VALUES (" +
                         std::to_string(account_id) + "," + std::to_string(clan_id) + ",'" + account_name + "')";
        result = PQexec(permanent_connection, i1.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::cout << "Chyba insert players_all " << PQresultErrorMessage(result) << std::endl;
        }
        PQclear(result);
        stat.new_account_id++;
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

} // namespace PlayerProcessingDebug
