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
    int empty_clans_closed = 0;    // rozpustene klany z clan_all_empty, ktorym sa uzavrel members_role
    int empty_rows_closed = 0;     // pocet takto uzavretych zaznamov members_role
    int empty_clans_unmarked = 0;  // zive klany odstranene z clan_all_empty
    int skipped_clans = 0;         // klany preskocene, lebo WG API pre ne trvalo vracia chybu

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
    void novy_v_klane_members_role(int account_id, int clan_id, const std::string& role, int joined_at);
    void nie_je_v_klane_members_role(int account_id);
    void zmena_roly_members_role(int account_id, int clan_id, const std::string& role);
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
// max_attempts <= 0 = opakovat donekonecna; vrati false, ak vsetky pokusy zlyhali
bool SendPost(const std::string& clan_id, std::string* json, int max_attempts = 0);
// SendPost pre davku 100 klanov; klany, pre ktore WG trvalo vracia chybu, z davky vyradi
void SendPostBatch(std::string* clan_id, int a_clan_id[100], std::string* json, Statistika& stat);
void ClearArray(int a_clan_id[100]);

// Klany v clan_all_empty, ktore maju otvorene zaznamy v members_role, overi cez WG API:
//  - rozpusteny / null / 0 clenov -> uzavrie members_role dnesnym dnom (odchod na /odchody),
//    players_all.clan_id = 0
//  - zivy -> odstrani ho z clan_all_empty, aby ho Players znova spracoval
// Volat pred vyberom klanov (GetClanId), aby sa odznacene klany spracovali uz v tomto behu.
void SkontrolujPrazdneKlany(PGconn* conn, Statistika& stat);

// Prepared statements
void PreparedSelectPlayersAll(PGconn* conn);
void PreparedSelectMembersRole(PGconn* conn);

} // namespace PlayerProcessing

#endif
