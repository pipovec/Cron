/*
*   Trieda pre spracovanie klanov - optimalizovana verzia
*   - Batch processing pre DB operacie
*   - Efektivne porovnavanie a update
*   - Thread-safe operacie
*   - C++17 features (optional, string_view)
*
*   author: Boris Fekar
*   date: 2025-11-15
*/

#ifndef CLANPROCESSOR_H
#define CLANPROCESSOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <string_view>
#include <libpq-fe.h>
#include "ClanData.h"
#include "../Pgsql.h"
#include "../../json/src/json.hpp"

using namespace std;
using json = nlohmann::json;

class ClanProcessor {
private:
    Pgsql* db;
    PGconn* conn;

    // Cache existujucich klanov z DB
    unordered_map<string, ClanData> existing_clans;

    // Sledovanie vsetkych clan_id z API (aj prazdnych, aj plnych)
    unordered_set<string> processed_clan_ids;

    // Batch buffery
    vector<ClanData> insert_batch;
    vector<ClanData> update_batch;
    vector<string> disbanded_clans;  // Rozpustene klany (iba clan_id v API)

    mutex batch_mutex;

    const int BATCH_SIZE = 100;

public:
    ClanProcessor(Pgsql* database);
    ~ClanProcessor();

    // Nacita existujuce klany z DB do cache
    void LoadExistingClans();

    // Spracuje JSON data a parsuje klany
    void ProcessJsonData(const string& json_string, vector<ClanData>& clans);

    // Porovna clan s existujucimi datami a rozhodne o insert/update
    void ProcessClan(const ClanData& clan);

    // Flush batchov do DB
    void FlushBatches();

    // Batch insert
    void BatchInsert(const vector<ClanData>& clans);

    // Batch update
    void BatchUpdate(const vector<ClanData>& clans);

    // Oznaci klany ktore uz neexistuju v API ako neaktivne
    void MarkInactiveClans();

    // Ziska pocet neaktivnych klanov
    int GetInactiveClansCount() const;

private:
    // Porovnanie dvoch klanov
    bool NeedsUpdate(const ClanData& new_data, const ClanData& old_data) const;
};

#endif
