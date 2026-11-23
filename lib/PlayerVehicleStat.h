/*
 * PlayerVehicleStat - stav hraca po tankoch (battles, wins, mark_of_mastery) z WoT API
 * (/account/tanks/) a jeho zapis do player_vehicle_stat. Nahradza PHP cron:player-vehicle-stat.
 */

#ifndef PLAYERVEHICLESTAT_H
#define PLAYERVEHICLESTAT_H

#include <libpq-fe.h>
#include <string>
#include <vector>

#include "./SendCurl.h"

namespace PlayerVehicleStat
{

struct Row
{
    int account_id;
    int tank_id;
    int battles;
    int wins;
    int mark_of_mastery;
};

struct Batch
{
    std::vector<Row> rows;
    int dead_accounts = 0; // ucty, pre ktore API vratilo null (zmazany hrac)
};

// Spracuje surovu odpoved /account/tanks/. Ucet s null sa pocita ako mrtvy, ucet s prazdnym
// zoznamom sa len vynecha. false, ak odpoved nie je platny JSON so status=ok a objektom "data".
bool Parse(const std::string &raw, Batch *out);

// Stiahne tanky pre davku account_id (max 100). false pri sietovej chybe alebo zlej odpovedi.
bool Fetch(SendCurl &send, const std::vector<int> &ids, Batch *out);

struct UpsertCount
{
    long long inserted = 0;
    long long updated = 0;
};

// Jedinym prikazom zapise riadky do player_vehicle_stat. Existujuci riadok sa meni len ak sa
// zmenili battles alebo mark_of_mastery (rovnako ako v povodnej PHP verzii).
// false pri chybe DB.
bool Upsert(PGconn *conn, const std::vector<Row> &rows, UpsertCount *count);

} // namespace PlayerVehicleStat

#endif
