/*
 * PlayerProcessor - Trieda pre spracovanie hracov klanov
 *
 * Zjednotenie logiky z PlayersCS, PlayersDE, PlayersPL, PlayersFR, PlayersAll
 *
 * Author: Boris Fekar
 * Date: 2025-11-19
 */

#ifndef PLAYERPROCESSOR_H
#define PLAYERPROCESSOR_H

#include <string>
#include <map>
#include <queue>
#include <libpq-fe.h>

namespace PlayerProcessing
{

// Statistika spracovania
struct Statistika {
    int new_players_all = 0;
    int new_account_id = 0;
    int change_nick = 0;
    int repair_members_role = 0;
    int new_members_role = 0;
    int change_role = 0;
    int empty_clan = 0;
    int players_left = 0;
    int clans_processed = 0;
    int players_checked = 0;

    void Print() const;
    void Reset();
};

// Struktura hraca
struct Player {
    int account_id;
    int joined_at;
    std::string account_name;
    std::string role_i18n;
};

// Typ pre mapu hracov
typedef std::map<int, Player> PlayerMap;
typedef std::queue<int> ClanQueue;

// Hlavna trieda pre spracovanie
class Spracuj
{
public:
    Spracuj(int clan_id[100], const std::string& json, PGconn* conn, Statistika& stat);
    ~Spracuj();

private:
    PGconn* permanent_connection;
    Statistika& stat;

    PlayerMap fresh;
    PlayerMap players_all;
    PlayerMap members_role;

    // Spracovanie
    void spracuj_players_all(int clan_id, const PlayerMap& players_all, const PlayerMap& fresh);
    void spracuj_members_role(int clan_id, const PlayerMap& members_role, const PlayerMap& fresh);

    // JSON spracovanie
    std::string spracuj_json_vyber_clan_id(const std::string& json, int clan_id, int* members_count, std::string* empty_reason);
    void spracuj_json_map(const std::string& clan_data, int* members_count);

    // Databazove operacie
    void novy_v_klane_members_role(int account_id, int clan_id, const std::string& role);
    void nie_je_v_klane_members_role(int account_id);
    void zmena_nick_players_all(const std::string& old_name, const std::string& new_name, int account_id);
    void nie_je_v_klane_players_all(int account_id);
    void novy_v_klane_players_all(int account_id, int clan_id, const std::string& account_name);
    void clan_is_empty(int cid, const std::string& reason);
    void update_players_all_empty(int cid);
    void update_clan_all_members_count(int cid, int members_count);

    // Select operacie
    void exec_select_players_all(int clan_id);
    void exec_select_members_role(int clan_id);
};

// Pomocne funkcie
void GetClanId(ClanQueue* p_aids, PGconn* permanent_connection, const std::string& query);
void Get100Id(std::string* p_clan_id, ClanQueue* p_aids, int a_clan_id[100]);
void SendPost(const std::string& clan_id, std::string* json);
void ClearArray(int a_clan_id[100]);

// Prepared statements
void PreparedSelectPlayersAll(PGconn* conn);
void PreparedSelectMembersRole(PGconn* conn);

} // namespace PlayerProcessing

#endif
