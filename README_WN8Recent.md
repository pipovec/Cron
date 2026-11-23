# WN8 za 7 / 14 / 28 dni

Tri programy, ktore pocitaju "recent" WN8 hracov z dennych prirastkov bitiek po tankoch.
Vzorec WN8 je zdielany s `PlayersWN8v2` (`lib/Wn8Formula.{h,cpp}`).

## Ako to funguje

1. **`PlayersVehicleSnapshot`** (jednorazovy bootstrap) – stiahne `account/tanks` pre vsetkych
   hracov a ulozi stav do `player_vehicle_snapshot` (cca 270 M riadkov, ~25 GB).
2. **`PlayersVehicleDelta`** (denne) – pre hracov, ktori maju v `players_stat_all_history` novu
   historiu, stiahne `account/tanks`, porovna so snapshotom a rozdiel (battles, wins po tankoch)
   zapise do `player_vehicle_daily`. Snapshot sa aktualizuje, `player_vehicle_sync.synced_date`
   posunie.
3. **`PlayersWN8Recent`** (denne, po Delta) – pre okna 7/14/28 dni spocita WN8 a ulozi do
   `wn8player_recent`.

Vypocet: citatel (wins, damage, frags, spot, def) pochadza zo suctov `players_stat_all_history`
za okno; menovatel je Σ `expected_tank_value × dennych prirastkov bitiek` z `player_vehicle_daily`.
Pocita sa len random bitky (`account/tanks` ich tak vracia).

## Tabulky (`sql/`)

| Subor | Tabulky |
|---|---|
| `001_player_vehicle_tables.sql` | `player_vehicle_snapshot`, `player_vehicle_daily` |
| `002_player_vehicle_sync.sql` | `player_vehicle_sync` (posledny spracovany den hraca) |
| `003_wn8player_recent.sql` | `wn8player_recent` (`account_id`, `period`, `wn8`, `battles`, `coverage`, `date_from`, `date_to`, `updated_at`) |

Vsetky maju FK na `players_all(account_id)` s `ON DELETE CASCADE` a `COMMENT ON` pre tabulky aj stlpce
(`\d+ tabulka`). Subory su idempotentne (`IF NOT EXISTS`).

## Nasadenie

```sh
make
psql -d clan -f sql/001_player_vehicle_tables.sql
psql -d clan -f sql/002_player_vehicle_sync.sql
psql -d clan -f sql/003_wn8player_recent.sql
./bin/PlayersVehicleSnapshot        # jednorazovo, ~1 h; da sa prerusit a znova spustit
```

Potom cron (po bootstrape): `PlayersVehicleDelta` a `PlayersWN8Recent` bezia v nocnom retazci
`scripts/nightly_chain.sh` (denne 02:15) hned po `PlayersStatAll`:

```
15 02 * * * /home/deamon/projects/crons/scripts/nightly_chain.sh
```

Poradie: `PlayersStatAll` (okrem pondelka) -> `PlayersVehicleDelta` -> `PlayersWN8Recent` ->
`PlayersVehicleStat active` -> `PlayersWN8v2`. Kazdy krok zacne az po skonceni predchadzajuceho (historia musi byt
zapisana, kym ju cita Delta). Priebeh je v `/home/deamon/outputs/C++/NightlyChain.txt`.

Okno N dni je platne az ked je k dispozicii N dni dennych delt od prveho behu `PlayersVehicleDelta`.
Dovtedy hracov s neuplnym oknom vyradi `WN8R_MIN_COVERAGE`.

## Premenne prostredia

Pripojenie k DB cez `PG*` (pozri `README.md`), API kluc ako pri ostatnych programoch.

### PlayersVehicleSnapshot
| Premenna | Default | Vyznam |
|---|---|---|
| `VEHICLE_THREADS` | 4 | pocet vlakien, 1-16 (kazde ma vlastny `SendCurl` a DB spojenie) |
| `VEHICLE_LIMIT` | 0 | spracuj najviac N hracov (test) |
| `VEHICLE_AFTER_ACCOUNT_ID` | | zacni za tymto `account_id` |
| `VEHICLE_REFRESH` | 0 | 1 = prepis aj uz synchronizovanych hracov |

Obnovenie: program spracuje len hracov bez zaznamu v `player_vehicle_sync`, takze po preruseni
staci spustit znova. Na konci vypise `Hotovo: ... zlyhanych davok N` (ma byt 0).

### PlayersVehicleDelta
| Premenna | Default | Vyznam |
|---|---|---|
| `DELTA_THREADS` | 4 | pocet vlakien, 1-16 |
| `DELTA_LIMIT` | 0 | najviac N hracov |
| `DELTA_LOOKBACK_DAYS` | 40 | ako dlho dozadu hladat novu historiu |
| `DELTA_KEEP_DAYS` | 35 | kolko dni dennych delt sa uchovava |
| `DELTA_DRY_RUN` | | iba spocita, nevola API, nic nezapisuje |

Chrani sa advisory lockom (727001), takze dva behy naraz nehrozia. Podozrive delty
(`delta > 2*historia + 50`) sa zahodia.

### PlayersWN8Recent
| Premenna | Default | Vyznam |
|---|---|---|
| `WN8R_PERIODS` | `7,14,28` | ktore okna pocitat |
| `WN8R_MIN_BATTLES` | 20 | minimum bitiek v okne |
| `WN8R_MIN_COVERAGE` | 90 | minimalne pokrytie (%) bitiek z historie dennymi deltami |
| `WN8R_END_DATE` | dnes | koniec okna (`YYYY-MM-DD`) |
| `WN8R_DRY_RUN` | | nic nezapisuje |

## Skusobny beh

```sh
DELTA_DRY_RUN=1 ./bin/PlayersVehicleDelta
WN8R_DRY_RUN=1  ./bin/PlayersWN8Recent
```
