/*
 * PlayerVehicles - per-tank stav hraca z WoT API (/account/tanks/) a jeho ulozenie
 * do player_vehicle_snapshot. Zdielane pre bootstrap snapshotu a denny vypocet prirastkov.
 */

#ifndef PLAYERVEHICLES_H
#define PLAYERVEHICLES_H

#include <libpq-fe.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "./SendCurl.h"

namespace PlayerVehicles
{

struct TankRow
{
    int tank_id;
    int battles;
    int wins;
};

typedef std::unordered_map<long long, std::vector<TankRow>> AccountTanks;

// Spracuje surovu odpoved /account/tanks/. Ucty s null alebo prazdnym zoznamom tankov
// sa vynechaju. Vracia false, ak odpoved nie je platny JSON so status=ok a objektom "data".
bool ParseAccountTanks(const std::string &raw, AccountTanks *out);

// Stiahne tanky pre davku account_id (max 100). false pri sietovej chybe alebo zlej odpovedi.
bool FetchAccountTanks(SendCurl &send, const std::vector<long long> &ids, AccountTanks *out);

// Zapise stav do player_vehicle_snapshot (INSERT ... ON CONFLICT DO UPDATE) jednym prikazom.
// Existujuce riadky sa menia (a updated_at sa nastavi) len ak sa battles/wins zmenili.
// Vracia pocet vlozenych alebo zmenenych riadkov alebo -1 pri chybe.
long long UpsertSnapshot(PGconn *conn, const AccountTanks &tanks);

} // namespace PlayerVehicles

#endif
