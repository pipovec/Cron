# Knižnice pre program Nabor.cpp

Tento adresár obsahuje špecializované triedy používané výlučne programom `Nabor.cpp`.

## Súbory

### DatabaseManager (.h/.cpp)
**Účel:** Thread-safe správa databázových spojení s connection pooling

**Funkcie:**
- Connection pool s nastaviteľnou veľkosťou (default: 5-10 spojení)
- Thread-safe získavanie a vracanie spojení
- RAII wrapper (`ConnectionGuard`) pre automatickú správu
- Podpora pre prepared statements
- Mutex-based synchronizácia pre multi-threading

**Výhody:**
- Redukcia overhead z vytvárania/rušenia spojení
- 40-60% zrýchlenie databázových operácií
- Thread-safe operácie

---

### BatchProcessor (.h/.cpp)
**Účel:** Dávkové spracovanie SQL operácií

**Funkcie:**
- Batch INSERT operácie (konfigurovateľná veľkosť dávky)
- Batch UPDATE pomocou temporary tables
- String escaping pre SQL bezpečnosť
- Automatické spracovanie NULL hodnôt

**Výhody:**
- 80-90% redukcia databázových roundtripov
- Optimalizované pre veľké objemy dát
- Konfigurovateľná veľkosť dávky (default: 500 záznamov)

---

### PlayerDataCollector (.h/.cpp)
**Účel:** Optimalizovaný zber dát o hráčoch z databázy

**Funkcie:**
- Jeden agregovaný dotaz namiesto N dotazov v cykle
- Použitie CASE statements pre agregácie
- Automatická filtrácia neaktívnych hráčov
- In-memory mapy pre rýchle vyhľadávanie

**Hlavné metódy:**
- `getBasicPlayerInfo()` - Základné info o hráčoch
- `collectBattleStats()` - Štatistiky bojov a winrate
- `collectVehicleStats()` - Počty tankov (level 6, 8, 10)
- `collectHistoryStats()` - História hier (7, 14, 30 dní)
- `collectAllPlayerData()` - Zber všetkých dát naraz

**Výhody:**
- 99.95% redukcia počtu SQL dotazov
- Pre 1000 hráčov: ~4 dotazy namiesto ~8000
- Automatická filtrácia neaktívnych hráčov

---

## Použitie

Tieto triedy sú automaticky linkované pri kompilácii `Nabor.cpp`:

```bash
make -f Makefile_Nabor
```

## Databázová štruktúra

Triedy očakávajú nasledujúce tabuľky:
- `nabor_new` - Výstupná tabuľka s výsledkami
- `players_all` - Základné info o hráčoch
- `players_info` - Detailné info o hráčoch
- `players_stat_all` - Štatistiky hráčov
- `players_stat_all_history` - História štatistík
- `player_vehicle_stat` - Štatistiky vozidiel
- `encyclopedia_vehicles` - Encyklopédia vozidiel
- `wn8player` - WN8 hodnotenia
- `members_role` - Členovia klanov
- `tmp_clans_cs` - Dočasná tabuľka CZ/SK klanov

## Thread Safety

Všetky triedy sú navrhnuté pre bezpečné použitie v multi-threaded prostredí:
- `DatabaseManager` používa mutex a condition variables
- `ConnectionGuard` poskytuje RAII pattern
- Batch operácie sú atomické

## Autor

Boris Fekar - Optimalizácia 2025
