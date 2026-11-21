# KontrolaKlanov

Program na kontrolu a aktualizáciu klanov z Wargaming API do PostgreSQL databázy.

## Popis

Program sťahuje údaje o klanoch pomocou Wargaming API (`/clans/list/`) a kontroluje, či sa nezmenili údaje v databáze. Ak áno, vykoná update, ak klan neexistuje, pridá ho do databázy.

## Optimalizácie

### 1. Batch Processing pre DB operácie (100 klanov naraz)
- **Pred:** N samostatných DB dotazov (prepared statement pre každý klan)
- **Po:** N/100 batch queries
- **Zrýchlenie:** ~100× menej DB round-trips

### 2. Hash mapa pre O(1) lookup
- **Pred:** Prepared statement SELECT pre každý klan
- **Po:** Jedno načítanie na začiatku + O(1) lookup v pamäti
- **Zrýchlenie:** Eliminácia 50,000+ DB dotazov

### 3. API Rate Limiting
- Rešpektuje Wargaming API limit: **max 15 requestov/sekundu**
- Automatické čakanie medzi requestmi (70ms = ~14.3 req/s)

### 4. Využitie existujúcich knižníc
- DB pripojenie: `lib/Pgsql.h` a `lib/Pgsql.cpp`
- HTTP client: `lib/SendCurl.h` a `lib/SendCurl.cpp`
- JSON parser: `json/src/json.hpp` (nlohmann/json v3.12.0)

### 5. Modulárna štruktúra
```
KontrolaKlanov/
├── ClanData.h         # Trieda pre údaje o klane
├── ClanData.cpp
├── ClanProcessor.h    # Hlavná trieda pre spracovanie s batch processing
├── ClanProcessor.cpp
└── README.md          # Tento súbor
```

## Požiadavky

### Systémové závislosti
```bash
sudo apt-get install build-essential libpq-dev libcurl4-openssl-dev
```

### Knižnice
- PostgreSQL libpq
- libcurl
- C++11 alebo novší
- nlohmann/json (už zahrnuté v `json/src/json.hpp`)

## Kompilácia

### Jednoduchá kompilácia
```bash
cd /home/deamon/Development/Cron
make KontrolaKlanov
```

### Clean build (zmazanie starých objektových súborov)
```bash
make clean
make KontrolaKlanov
```

### Manuálna kompilácia (ak makefile nefunguje)
```bash
# Kompiluj triedy
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql ./KontrolaKlanov/ClanData.cpp -o ./KontrolaKlanov/ClanData.o
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql ./KontrolaKlanov/ClanProcessor.cpp -o ./KontrolaKlanov/ClanProcessor.o

# Kompiluj knižnice
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql ./lib/Pgsql.cpp -o ./lib/Pgsql.o
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql ./lib/SendCurl.cpp -o ./lib/SendCurl.o

# Kompiluj hlavný program
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql KontrolaKlanov.cpp -o ./KontrolaKlanov.o

# Linkuj všetko dokopy
g++ -o ./bin/KontrolaKlanov ./KontrolaKlanov.o ./KontrolaKlanov/ClanData.o ./KontrolaKlanov/ClanProcessor.o ./lib/SendCurl.o ./lib/Pgsql.o -L/usr/lib/x86_64-linux-gnu -lcurl -lpq -lpthread
```

## Spustenie

```bash
./bin/KontrolaKlanov
```

## Konfigurácia databázy

Databázové pripojenie je definované v `lib/Pgsql.h`:
```cpp
const char *conninfo = "host=/var/run/postgresql port=5433 dbname=clan user=deamon password=sedemtri";
```

### Potrebné DB tabuľky

#### clan_all
```sql
CREATE TABLE clan_all (
    clan_id INTEGER PRIMARY KEY,
    abbreviation VARCHAR(10),
    created_at INTEGER,
    name VARCHAR(255),
    members_count INTEGER,
    emblems_large TEXT,
    emblems_medium TEXT,
    emblems_small TEXT,
    emblems_tank TEXT
);
```

## Výkon

### Očakávané zrýchlenie

| Metriky | Pôvodná verzia | Optimalizovaná | Zlepšenie |
|---------|----------------|----------------|-----------|
| DB queries | ~50,000+ | ~500-1,000 | **50-100×** |
| Čas spracovania | 10-20 min | **1-2 min** | **10×** |
| Pamäť | Nízka | Stredná (cache) | +50 MB |
| Zložitosť lookup | O(n) | O(1) | **n×** |
| API rate limit | ❌ Žiadny | ✅ 15 req/s | Bezpečné |

## Výstup programu

```
*********************************
Program KontrolaKlanov
API Rate Limit: max 15 req/s
*********************************

Nacitavam existujuce klany z databazy...
Nacitanych 12543 existujucich klanov

Zacinam stahovanie dat zo servera...
Celkovy pocet klanov: 52341
Celkovy pocet stran: 524
Spracovavam stranku 1/524 (100 klanov)
Batch insert: 15 klanov
Batch update: 12 klanov
Spracovavam stranku 2/524 (100 klanov)
...

Ukladam posledne zmeny...
Batch insert: 23 klanov
Batch update: 8 klanov

*********************************
Program uspesne dokonceny
Celkovy cas: 87 sekund
*********************************
```

## Riešenie problémov

### Chyba pri kompilácii: "cannot find -lpq"
```bash
sudo apt-get install libpq-dev
```

### Chyba pri kompilácii: "cannot find -lcurl"
```bash
sudo apt-get install libcurl4-openssl-dev
```

### Chyba pri spustení: "Connection to database failed"
Skontrolujte:
1. PostgreSQL beží: `sudo systemctl status postgresql`
2. Databáza existuje: `psql -U deamon -d clan -c "\dt"`
3. Connection string v `lib/Pgsql.h` je správny

### Program je príliš pomalý
Skontrolujte:
1. API rate limiting nespôsobuje zbytočné čakanie
2. Databázové indexy na `clan_id` kolónke
3. Sieťové pripojenie k Wargaming API

## Autor

Boris Fekár (pipovec, BobyOneKiller)

**Optimalizácia:** 2025-11-15

## Licencia

Proprietárny softvér
