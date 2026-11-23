# PlayersVehicleStat

C++ nahrada PHP prikazu `cron:player-vehicle-stat` (admin). Plni tabulku `player_vehicle_stat`
(`account_id`, `tank_id`, `battles`, `wins`, `mark_of_mastery`) z WoT API `/account/tanks/`.
Tabulka drzi len aktualny stav (bez historie); cita ju nabor, `RecruitingRepository` v admine
a materializovany pohlad `mv_player_vehicle_summary`.

## Ako to funguje

1. Nacita zoznam uctov raz do pamate:
   - `active` - hraci s riadkom v `players_stat_all_history` za `VSTAT_DATE` (default vcera),
   - `full` - vsetci z `players_stat_all`.
2. N vlakien (kazde ma vlastne DB spojenie a vlastny curl) si atomicky bera davky po 100 uctov.
3. Kazda davka je jedno API volanie a jeden SQL prikaz (`INSERT ... ON CONFLICT DO UPDATE`
   nad `unnest`), ktory zapise len riadky, kde sa zmenilo `battles` alebo `mark_of_mastery`.
   Pocet vlozenych a aktualizovanych riadkov sa pocita v tom istom prikaze (na partitionovanej
   tabulke nejde `RETURNING xmax`).

Zlyhana davka (siet, API, DB) sa preskoci a spocita; zvysok dobehne. Ucty, pre ktore API vrati
`null`, sa pocitaju ako mrtve (nic sa s nimi nerobi). Tanky, ktore hrac uz nema v garazi, sa
nemazu.

## Spustenie

```sh
make PlayersVehicleStat
./bin/PlayersVehicleStat active     # denne (cron)
./bin/PlayersVehicleStat full       # jednorazovo / dohnanie (~1,5 h s 8 vlaknami)
```

Dlhy beh na pozadi:

```sh
nohup ./bin/PlayersVehicleStat full > /home/deamon/outputs/C++/PlayersVehicleStatFull.txt 2>&1 &
```

Cron (po `PlayersStatAll`, ktory o 07:15 doplni historiu za vcera):

```
00 08 * * * cd .../crons && ./bin/PlayersVehicleStat active > .../PlayersVehicleStat.txt 2>&1
```

V pondelok nema historia za nedelu (`PlayersStatAll` bezi utorok az nedela), takze `active`
najde len par hracov - je to ocakavane.

## Premenne prostredia

Pripojenie k DB cez `PG*` (pozri `README.md`), API kluc ako pri ostatnych programoch.

| Premenna | Default | Vyznam |
|---|---|---|
| `VSTAT_MODE` | `active` | `active` alebo `full` (argument programu ma prednost) |
| `VSTAT_DATE` | vcera | den historie pre rezim `active`, `YYYY-MM-DD` |
| `VSTAT_THREADS` | 8 | pocet vlakien, 1-16 |
| `VSTAT_LIMIT` | 0 | najviac N hracov (test) |
| `VSTAT_DRY_RUN` | | 1 = stiahne a spocita, nic nezapisuje |

Navratovy kod: 0 ok, 1 chyba DB, 2 niektore davky zlyhali. Do `cron_log` sa zapisuje ako
`cplusplus:PlayersVehicleStat` (Hraci, Vlozenych, Aktualizovanych, Mrtvych, Preskocenych).

## Priepustnost

Cas je dany latenciou API (~1 s na davku), preto skaluje s poctom vlakien: 2000 hracov trva
25 s s 1 vlaknom, 5 s s 8 a 3,4 s so 16.
