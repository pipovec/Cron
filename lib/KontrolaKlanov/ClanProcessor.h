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

    // Batch buffery
    vector<ClanData> insert_batch;
    vector<ClanData> update_batch;

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

private:
    // Porovnanie dvoch klanov
    bool NeedsUpdate(const ClanData& new_data, const ClanData& old_data) const;
};

#endif
