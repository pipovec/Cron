# lib/ - Zdieľané knižnice

Tento adresár obsahuje zdieľané knižnice používané naprieč projektom.

## Pgsql - PostgreSQL Database Wrapper

Trieda na pripojenie a prácu s PostgreSQL databázou.

### Optimalizácie (2025-11-15)

#### ✅ Opravené kritické problémy:
1. **Preklep:** `Disconnet()` → `Disconnect()`
2. **Double free:** Odstránený duplicitný `PQfinish()` v deštruktore
3. **Memory leak:** Správne uvoľňovanie pri Disconnect()

#### ✅ Nové funkcionality:
- **Reconnect logika** - Automatické obnovenie spojenia
- **Prepared statements** - Podpora pre efektívne dotazy
- **Transakcie** - BEGIN/COMMIT/ROLLBACK
- **Health check** - IsConnected() a Ping()
- **Lepší error handling** - Kontrola výsledkov všetkých operácií

### API Dokumentácia

#### Základné metódy

```cpp
// Vytvorenie a pripojenie
Pgsql* db = new Pgsql();

// Získanie connection pointeru
PGconn* conn = db->Get();

// Odpojenie
db->Disconnect();

// Automaticky sa odpojí v deštruktore
delete db;
```

#### Query metódy

```cpp
// Jednoduchý query
PGresult* result = db->Query("SELECT * FROM users");
if (result != nullptr) {
    // Spracuj vysledok
    PQclear(result);
}

// Query s parametrami (bezpečnejšie, prevencia SQL injection)
const char* paramValues[] = {"123"};
PGresult* result = db->QueryParams(
    "SELECT * FROM users WHERE id = $1",
    1,
    paramValues
);
```

#### Prepared Statements

```cpp
// Priprav statement
PGresult* prep = db->Prepare(
    "select_user",                           // Názov
    "SELECT * FROM users WHERE id = $1",    // Query
    1                                        // Počet parametrov
);
PQclear(prep);

// Vykonaj prepared statement (opakovane, efektívne)
const char* params[] = {"123"};
PGresult* result = db->ExecutePrepared("select_user", 1, params);
if (result != nullptr) {
    // Spracuj vysledok
    PQclear(result);
}
```

#### Transakcie

```cpp
if (db->BeginTransaction()) {
    PGresult* r1 = db->Query("INSERT INTO users ...");
    PGresult* r2 = db->Query("UPDATE accounts ...");

    if (r1 && r2) {
        db->CommitTransaction();
    } else {
        db->RollbackTransaction();
    }

    if (r1) PQclear(r1);
    if (r2) PQclear(r2);
}
```

#### Health Check

```cpp
// Kontrola stavu spojenia
if (!db->IsConnected()) {
    cout << "Spojenie stratene!" << endl;
}

// Ping databázy (SELECT 1)
if (db->Ping()) {
    cout << "Databaza odpoveda" << endl;
}
```

### Automatický Reconnect

Metóda `Get()` automaticky detekuje stratené spojenie a pokúsi sa o reconnect:

```cpp
PGconn* conn = db->Get();
// Ak spojenie zlyhalo, Get() sa pokúsi o reconnect
// Ak reconnect uspeje, vráti platné spojenie
// Ak reconnect zlyhá, vráti nullptr
```

### Príklady použitia

#### Jednoduchý SELECT

```cpp
Pgsql* db = new Pgsql();

PGresult* result = db->Query("SELECT clan_id, name FROM clan_all LIMIT 10");

if (result != nullptr && PQresultStatus(result) == PGRES_TUPLES_OK) {
    int rows = PQntuples(result);

    for (int i = 0; i < rows; i++) {
        string clan_id = PQgetvalue(result, i, 0);
        string name = PQgetvalue(result, i, 1);

        cout << clan_id << ": " << name << endl;
    }

    PQclear(result);
}

delete db;
```

#### Batch INSERT s transakciou

```cpp
Pgsql* db = new Pgsql();

if (db->BeginTransaction()) {
    string values;
    for (int i = 0; i < 100; i++) {
        values += "('" + to_string(i) + "','data" + to_string(i) + "'),";
    }
    values.pop_back(); // Odstráň poslednú čiarku

    string query = "INSERT INTO table (id, data) VALUES " + values;
    PGresult* result = db->Query(query.c_str());

    if (result != nullptr && PQresultStatus(result) == PGRES_COMMAND_OK) {
        db->CommitTransaction();
        cout << "Batch insert uspešný" << endl;
    } else {
        db->RollbackTransaction();
        cout << "Batch insert zlyhal" << endl;
    }

    if (result) PQclear(result);
}

delete db;
```

#### Bezpečný query s parametrami

```cpp
Pgsql* db = new Pgsql();

string user_input = "'; DROP TABLE users; --"; // SQL injection pokus

const char* params[] = {user_input.c_str()};
PGresult* result = db->QueryParams(
    "SELECT * FROM users WHERE username = $1",
    1,
    params
);

// Bezpečné - user_input je escapovaný
if (result != nullptr) {
    // Spracuj
    PQclear(result);
}

delete db;
```

### Výhody oproti pôvodnej verzii

| Vlastnosť | Pôvodná verzia | Optimalizovaná |
|-----------|----------------|----------------|
| Preklepy | ❌ Disconnet() | ✅ Disconnect() |
| Double free | ❌ Áno | ✅ Nie |
| Reconnect | ❌ Nie | ✅ Automatický |
| Error handling | ❌ Slabé | ✅ Kompletné |
| Prepared stmts | ❌ Nie | ✅ Podpora |
| Transakcie | ❌ Nie | ✅ BEGIN/COMMIT/ROLLBACK |
| Health check | ❌ Nie | ✅ IsConnected/Ping |
| Query validation | ❌ Žiadna | ✅ Kontrola výsledkov |

### Kompatibilita

Všetky pôvodné metódy fungujú ako predtým, takže existujúci kód bude fungovať bez zmien:

```cpp
// Pôvodný kód stále funguje
Pgsql* p = new Pgsql();
PGconn* conn = p->pgsql;
PGresult* res = p->Query("SELECT ...");
```

### Connection String

Definovaný v `Pgsql.h`:
```cpp
const char *conninfo = "host=/var/run/postgresql port=5433 dbname=clan user=deamon password=sedemtri";
```

Pre zmenu DB pripojenia upravte tento riadok.

## SendCurl - HTTP Client

(TODO: Dokumentácia pre SendCurl)

## Kompilácia knižníc

Knižnice sa kompilujú automaticky pomocou makefile:

```bash
make -f makeKontrolaKlanov
```

Alebo manuálne:

```bash
g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql ./lib/Pgsql.cpp -o ./lib/Pgsql.o
g++ -c --std=c++11 -Wall -O3 ./lib/SendCurl.cpp -o ./lib/SendCurl.o
```

## Závislos
ti

```bash
sudo apt-get install libpq-dev libcurl4-openssl-dev
```
