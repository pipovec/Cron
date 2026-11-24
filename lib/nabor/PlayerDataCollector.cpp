#include "PlayerDataCollector.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <map>

std::string PlayerDataCollector::calculateWinrate(const std::string& battles, const std::string& wins)
{
    if (battles.empty() || wins.empty()) return "0";

    try {
        int b = std::stoi(battles);
        int w = std::stoi(wins);

        if (b == 0) return "0";

        float wr = (static_cast<float>(w) / static_cast<float>(b)) * 100.0f;
        return std::to_string(wr);
    } catch (...) {
        return "0";
    }
}

std::vector<PlayerData> PlayerDataCollector::getBasicPlayerInfo()
{
    std::vector<PlayerData> players;
    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) {
        std::cerr << "Chyba spojenia v getBasicPlayerInfo" << std::endl;
        return players;
    }

    // Optimalizovaný dotaz - získa všetky základné info naraz
    const char* query = R"(
        SELECT DISTINCT
            foo.account_id,
            players_all.nickname,
            COALESCE(ps_gr.global_rating, 0) as global_rating,
            COALESCE(wn8player.wn8, 0) as wn8
        FROM (
            SELECT DISTINCT account_id
            FROM members_role
            WHERE clan_id IN (SELECT clan_id FROM tmp_clans_cs)
        ) as foo
        JOIN players_all ON foo.account_id = players_all.account_id
        LEFT JOIN players_stat_all ps_gr ON foo.account_id = ps_gr.account_id
        LEFT JOIN wn8player ON foo.account_id = wn8player.account_id
        WHERE players_all.clan_id = 0
    )";

    PGresult* result = dbManager->executeQuery(conn, query);

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba query basic info: "
                  << (result ? PQresultErrorMessage(result) : "NULL") << std::endl;
        if (result) PQclear(result);
        return players;
    }

    int rows = PQntuples(result);
    players.reserve(rows);

    for (int i = 0; i < rows; ++i) {
        PlayerData player;
        player.account_id = PQgetvalue(result, i, 0);
        player.nickname = PQgetvalue(result, i, 1);
        player.global_rating = PQgetvalue(result, i, 2);
        player.wn8 = PQgetvalue(result, i, 3);

        players.push_back(player);
    }

    PQclear(result);
    return players;
}

bool PlayerDataCollector::collectBattleStats(std::vector<PlayerData>& players)
{
    if (players.empty()) return true;

    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) return false;

    // Vytvor IN clause s account_ids
    std::ostringstream accountIds;
    for (size_t i = 0; i < players.size(); ++i) {
        if (i > 0) accountIds << ",";
        accountIds << players[i].account_id;
    }

    // Jeden dotaz pre všetkých hráčov
    std::ostringstream query;
    query << "SELECT account_id, battles, wins, battle_avg_xp "
          << "FROM players_stat_all "
          << "WHERE account_id IN (" << accountIds.str() << ")";

    PGresult* result = dbManager->executeQuery(conn, query.str());

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba query battle stats" << std::endl;
        if (result) PQclear(result);
        return false;
    }

    // Vytvor mapu pre rýchle vyhľadávanie
    std::map<std::string, std::tuple<std::string, std::string, std::string>> statsMap;

    int rows = PQntuples(result);
    for (int i = 0; i < rows; ++i) {
        std::string account_id = PQgetvalue(result, i, 0);
        std::string battles = PQgetvalue(result, i, 1);
        std::string wins = PQgetvalue(result, i, 2);
        std::string avgxp = PQgetvalue(result, i, 3);

        std::string winrate = calculateWinrate(battles, wins);

        statsMap[account_id] = std::make_tuple(battles, winrate, avgxp);
    }

    PQclear(result);

    // Priradí štatistiky hráčom
    for (auto& player : players) {
        auto it = statsMap.find(player.account_id);
        if (it != statsMap.end()) {
            player.battles_all = std::get<0>(it->second);
            player.winrate = std::get<1>(it->second);
            player.avgxp = std::get<2>(it->second);
        }
    }

    return true;
}

bool PlayerDataCollector::collectVehicleStats(std::vector<PlayerData>& players)
{
    if (players.empty()) return true;

    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) return false;

    // Vytvor IN clause
    std::ostringstream accountIds;
    for (size_t i = 0; i < players.size(); ++i) {
        if (i > 0) accountIds << ",";
        accountIds << players[i].account_id;
    }

    // Optimalizovaný dotaz - počíta všetky levely naraz pomocou CASE
    std::ostringstream query;
    query << R"(
        SELECT
            pvs.account_id,
            COUNT(CASE WHEN ev.level = 10 THEN 1 END) as pocet_x,
            COUNT(CASE WHEN ev.level = 8 THEN 1 END) as pocet_8,
            COUNT(CASE WHEN ev.level = 6 THEN 1 END) as pocet_6
        FROM player_vehicle_stat pvs
        JOIN encyclopedia_vehicles ev ON pvs.tank_id = ev.tank_id
        WHERE pvs.account_id IN ()" << accountIds.str() << R"()
          AND ev.level IN (6, 8, 10)
        GROUP BY pvs.account_id
    )";

    PGresult* result = dbManager->executeQuery(conn, query.str());

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba query vehicle stats" << std::endl;
        if (result) PQclear(result);
        return false;
    }

    // Vytvor mapu
    std::map<std::string, std::tuple<std::string, std::string, std::string>> vehicleMap;

    int rows = PQntuples(result);
    for (int i = 0; i < rows; ++i) {
        std::string account_id = PQgetvalue(result, i, 0);
        std::string pocet_x = PQgetvalue(result, i, 1);
        std::string pocet_8 = PQgetvalue(result, i, 2);
        std::string pocet_6 = PQgetvalue(result, i, 3);

        vehicleMap[account_id] = std::make_tuple(pocet_x, pocet_8, pocet_6);
    }

    PQclear(result);

    // Priradí údaje hráčom
    for (auto& player : players) {
        auto it = vehicleMap.find(player.account_id);
        if (it != vehicleMap.end()) {
            player.pocet_x = std::get<0>(it->second);
            player.pocet_8 = std::get<1>(it->second);
            player.pocet_6 = std::get<2>(it->second);
        } else {
            player.pocet_x = "0";
            player.pocet_8 = "0";
            player.pocet_6 = "0";
        }
    }

    return true;
}

bool PlayerDataCollector::collectHistoryStats(std::vector<PlayerData>& players)
{
    if (players.empty()) return true;

    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) return false;

    // IN clause
    std::ostringstream accountIds;
    for (size_t i = 0; i < players.size(); ++i) {
        if (i > 0) accountIds << ",";
        accountIds << players[i].account_id;
    }

    // Jeden optimalizovaný dotaz pre všetky časové obdobia
    std::ostringstream query;
    query << R"(
        SELECT
            account_id,
            SUM(CASE WHEN date > now() - interval '7 day' THEN battles ELSE 0 END) as last7,
            SUM(CASE WHEN date > now() - interval '14 day' THEN battles ELSE 0 END) as last14,
            SUM(CASE WHEN date > now() - interval '30 day' THEN battles ELSE 0 END) as last30
        FROM players_stat_all_history
        WHERE account_id IN ()" << accountIds.str() << R"()
          AND date > now() - interval '30 day'
        GROUP BY account_id
    )";

    PGresult* result = dbManager->executeQuery(conn, query.str());

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::cerr << "Chyba query history stats" << std::endl;
        if (result) PQclear(result);
        return false;
    }

    // Vytvor mapu
    std::map<std::string, std::tuple<std::string, std::string, std::string>> historyMap;

    int rows = PQntuples(result);
    for (int i = 0; i < rows; ++i) {
        std::string account_id = PQgetvalue(result, i, 0);
        std::string last7 = PQgetvalue(result, i, 1);
        std::string last14 = PQgetvalue(result, i, 2);
        std::string last30 = PQgetvalue(result, i, 3);

        // Ak je NULL, nastav na 0
        if (last7.empty()) last7 = "0";
        if (last14.empty()) last14 = "0";
        if (last30.empty()) last30 = "0";

        historyMap[account_id] = std::make_tuple(last7, last14, last30);
    }

    PQclear(result);

    // Priradí údaje a odstráň neaktívnych hráčov
    auto it = players.begin();
    while (it != players.end()) {
        auto histIt = historyMap.find(it->account_id);
        if (histIt != historyMap.end()) {
            it->last7 = std::get<0>(histIt->second);
            it->last14 = std::get<1>(histIt->second);
            it->last30 = std::get<2>(histIt->second);

            // Odstráň hráčov bez aktivít za 30 dní
            try {
                if (std::stoi(it->last30) < 1) {
                    it = players.erase(it);
                    continue;
                }
            } catch (...) {
                it = players.erase(it);
                continue;
            }
        } else {
            // Žiadne dáta = odstráň hráča
            it = players.erase(it);
            continue;
        }
        ++it;
    }

    return true;
}

std::vector<PlayerData> PlayerDataCollector::collectAllPlayerData()
{
    std::cout << "Zbieram zakladne info o hracoch..." << std::endl;
    std::vector<PlayerData> players = getBasicPlayerInfo();

    if (players.empty()) {
        std::cout << "Ziadni hraci nenajdeni" << std::endl;
        return players;
    }

    std::cout << "Najdenych " << players.size() << " hracov" << std::endl;

    std::cout << "Zbieram battle statistiky..." << std::endl;
    collectBattleStats(players);

    std::cout << "Zbieram vehicle statistiky..." << std::endl;
    collectVehicleStats(players);

    std::cout << "Zbieram history statistiky..." << std::endl;
    collectHistoryStats(players);

    std::cout << "Po filtraci zostalo " << players.size() << " aktivnych hracov" << std::endl;

    return players;
}
