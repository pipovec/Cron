# wotcron

C++ programy, ktore tahaju data cez Wargaming API a ukladaju ich do PostgreSQL.

## Struktura

- `src/` – hlavne programy (`main`)
- `lib/` – zdielane kniznice (Pgsql, SendCurl, procesory...)
- `json/` – git submodul [nlohmann/json](https://github.com/nlohmann/json) (v3.12.0)
- `bin/`, `obj/` – vystupy buildu (v `.gitignore`)

## Build

Zavislosti (Debian/Ubuntu): `g++ make libpq-dev libcurl4-openssl-dev`

```sh
git clone --recurse-submodules git@github.com:pipovec/Cron.git
make -j"$(nproc)"          # vsetky programy do bin/
make PlayersStatAll        # jeden program
make clean                 # zmaze obj/
```

Programy: `Players`, `PlayersStatAll`, `PlayersStatNaborNew`,
`PlayersStatAktualneOdchody`, `PlayersStatAktualneOdchodyPl`,
`PlayersStatDeparturesGermany`, `PlayersWN8v2`, `KontrolaKlanov`, `Vehicles`,
`PlayersVehicleSnapshot`, `PlayersVehicleDelta`, `PlayersWN8Recent`
(WN8 za 7/14/28 dni, pozri [README_WN8Recent.md](README_WN8Recent.md)),
`PlayersVehicleStat` (stav hracov po tankoch, pozri [README_VehicleStat.md](README_VehicleStat.md)).

## Konfiguracia

Skopiruj `.env.example` do `.env` (prava `chmod 600 .env`, subor je v `.gitignore`) a vypln:

- `PG*` – pripojenie k PostgreSQL
- `WG_APPLICATION_ID` – kluc k WoT API (developers.wargaming.net), pouziva ho `SendCurl`
- `MONGODB_URL` – MongoDB pre `PlayersStatAll` (dokumenty `fpcstat.player-stats`); `mongoexport`/`mongoimport`
  ho dostanu cez docasny konfiguracny subor, takze heslo nie je vidiet v zozname procesov

Subor sa hlada v aktualnom adresari, alebo podla `DB_CONFIG_PATH`. Premenna prostredia s rovnakym nazvom
ma prednost pred `.env`. Programy sa preto spustaju z korena projektu (`cd ~/projects/crons && ./bin/...`).

## CI

GitHub Actions (`.github/workflows/ci.yml`) zbuilduje vsetky programy pri kazdom pushi a pull requeste.
