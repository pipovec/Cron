# Players - Unifikovaný program pre spracovanie hráčov klanov

## Popis

Program Players zjednocuje funkčnosť viacerých pôvodných skriptov (PlayersCS, PlayersDE, PlayersPL, PlayersFR, PlayersAll) do jedného programu, ktorý sa spúšťa s parametrom určujúcim typ spracovania.

## Kompilácia

```bash
make -f Makefile_Players
```

### Ďalšie make príkazy

- `make -f Makefile_Players clean` - vymaže objektové súbory
- `make -f Makefile_Players distclean` - vymaže všetky vygenerované súbory
- `make -f Makefile_Players rebuild` - prebuduje celý projekt
- `make -f Makefile_Players info` - zobrazí informácie o projekte

## Použitie

```bash
./bin/Players <TYP>
```

### Podporované typy

- **CS** - České a Slovenské klany (language='cs' OR language='sk')
- **DE** - Nemecké klany (language='de')
- **PL** - Poľské klany (language='pl')
- **FR** - Francúzske klany (language='fr')
- **ALL** - Všetky ostatné klany (nie CS, DE, PL, FR)

### Príklady

```bash
# Spracovanie českých a slovenských klanov
./bin/Players CS

# Spracovanie nemeckých klanov
./bin/Players DE

# Spracovanie všetkých ostatných klanov
./bin/Players ALL
```

## Štruktúra projektu

```
C++/
├── Players.cpp              # Hlavný program
├── Makefile_Players         # Makefile pre kompiláciu
├── lib/
│   ├── Pgsql.h/cpp         # PostgreSQL databázové pripojenie
│   ├── SendCurl.h/cpp      # HTTP komunikácia s WG API
│   ├── QueryBuilder.h/cpp  # Generovanie SQL dotazov
│   └── PlayerProcessor.h/cpp # Spracovanie hráčov
├── bin/
│   └── Players             # Skompilovaný program
└── obj/
    └── *.o                 # Objektové súbory
```

## Technické detaily

- **C++ štandard**: C++17
- **Databáza**: PostgreSQL (libpq)
- **HTTP knižnica**: libcurl
- **Závislosti**: pthread

## Databázové pripojenie

Program používa triedu `Pgsql` z `lib/Pgsql.h`, ktorá sa automaticky pripojí k databáze pomocou konfigurácie definovanej v triede.

## Migrácia zo starých skriptov

### Pred:
```bash
./PlayersCS   # České a slovenské klany
./PlayersDE   # Nemecké klany
./PlayersPL   # Poľské klany
./PlayersFR   # Francúzske klany
./PlayersAll  # Ostatné klany
```

### Teraz:
```bash
./bin/Players CS   # České a slovenské klany
./bin/Players DE   # Nemecké klany
./bin/Players PL   # Poľské klany
./bin/Players FR   # Francúzske klany
./bin/Players ALL  # Ostatné klany
```

## Výhody unifikovaného riešenia

1. **Jednoduchšia údržba** - jeden kód namiesto päť skriptov
2. **Konzistentnosť** - všetky typy používajú rovnakú logiku
3. **Rozšíriteľnosť** - pridanie nového typu je jednoduché
4. **C++17 štandard** - modernejší a bezpečnejší kód
5. **Lepšia čitateľnosť** - čistejšia štruktúra s namespace a triedami

## Štatistika spracovania

Program po dokončení vypíše štatistiku:
- Počet kôl spracovania
- Celkový čas behu
- Počet príchodov do klanu
- Počet úplne nových ID
- Počet zmien nickov
- Počet ukončených klanov

## Autor

Boris Fekar
Dátum: 2025-11-19

## Licencia

Podľa pôvodných skriptov.
