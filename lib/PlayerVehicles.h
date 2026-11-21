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

// tank_id -> (battles, wins) pre jedneho hraca; account_id -> taka mapa
typedef std::unordered_map<int, std::pair<int, int>> TankState;
typedef std::unordered_map<long long, TankState> SnapshotMap;

// Prirastok jedneho hraca: kladne rozdiely bitiek/vyhier po tankoch
struct DeltaResult
{
    std::vector<TankRow> rows; // battles/wins su PRIRASTKY
    long long battles = 0;     // sucet prirastkov bitiek
};

// Rozdiel medzi aktualnym stavom 'now' a ulozenym stavom 'old'. Tank bez ulozeneho stavu sa
// berie ako (0,0). Zaporne alebo nulove prirastky bitiek sa ignoruju.
DeltaResult ComputeDeltas(const TankState &old, const std::vector<TankRow> &now);

// Podozrivy prirastok: sucet prirastkov po tankoch je neprimerane vacsi nez bitky v historii
// (napr. predany tank, ktory sa vratil do garaze so starymi statistikami).
bool LooksSuspicious(long long delta_battles, long long history_battles);

// Spracuje surovu odpoved /account/tanks/. Ucty s null alebo prazdnym zoznamom tankov
// sa vynechaju. Vracia false, ak odpoved nie je platny JSON so status=ok a objektom "data".
bool ParseAccountTanks(const std::string &raw, AccountTanks *out);

// Stiahne tanky pre davku account_id (max 100). false pri sietovej chybe alebo zlej odpovedi.
bool FetchAccountTanks(SendCurl &send, const std::vector<long long> &ids, AccountTanks *out);

// Zapise stav do player_vehicle_snapshot (INSERT ... ON CONFLICT DO UPDATE) jednym prikazom.
// Existujuce riadky sa menia (a updated_at sa nastavi) len ak sa battles/wins zmenili.
// Vracia pocet vlozenych alebo zmenenych riadkov alebo -1 pri chybe.
long long UpsertSnapshot(PGconn *conn, const AccountTanks &tanks);

// Nacita ulozeny stav (player_vehicle_snapshot) pre davku uctov jednym dotazom.
bool LoadSnapshot(PGconn *conn, const std::vector<long long> &ids, SnapshotMap *out);

// Jeden riadok player_vehicle_daily; date je text vo formate YYYY-MM-DD
struct DailyRow
{
    std::string date;
    long long account_id;
    int tank_id;
    int battles;
    int wins;
};

// Zapise prirastky do player_vehicle_daily. Pri konflikte (rovnaky den, hrac, tank) prirastky
// SCITA. Vracia pocet dotknutych riadkov alebo -1 pri chybe.
long long UpsertDaily(PGconn *conn, const std::vector<DailyRow> &rows);

// Zapise "snapshot hraca je aktualny po datum" do player_vehicle_sync (date: YYYY-MM-DD).
long long UpsertSync(PGconn *conn, const std::vector<std::pair<long long, std::string>> &sync);

} // namespace PlayerVehicles

#endif
