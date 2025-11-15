#include "ClanProcessor.h"
#include <iostream>

ClanProcessor::ClanProcessor(Pgsql* database) : db(database) {
    conn = db->Get();
}

ClanProcessor::~ClanProcessor() {
    FlushBatches();
}

void ClanProcessor::LoadExistingClans() {
    const char* query = "SELECT clan_id, abbreviation, created_at, name, members_count, "
                       "emblems_large, emblems_medium, emblems_small, emblems_tank "
                       "FROM clan_all";

    PGresult* result = PQexec(conn, query);

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        cerr << "Chyba pri nacitani klanov: " << PQerrorMessage(conn) << endl;
        PQclear(result);
        return;
    }

    int ntuples = PQntuples(result);
    existing_clans.reserve(ntuples);

    for(int i = 0; i < ntuples; i++) {
        ClanData clan;
        clan.clan_id = PQgetvalue(result, i, 0);
        clan.tag = PQgetvalue(result, i, 1);
        clan.created_at = PQgetvalue(result, i, 2);
        clan.name = PQgetvalue(result, i, 3);
        clan.members_count = PQgetvalue(result, i, 4);
        clan.emblem195 = PQgetvalue(result, i, 5);
        clan.emblem64 = PQgetvalue(result, i, 6);
        clan.emblem32 = PQgetvalue(result, i, 7);
        clan.emblem24 = PQgetvalue(result, i, 8);

        existing_clans[clan.clan_id] = clan;
    }

    PQclear(result);
    cout << "Nacitanych " << existing_clans.size() << " existujucich klanov" << endl;
}

void ClanProcessor::ProcessJsonData(const string& json_string, vector<ClanData>& clans) {
    json js = json::parse(json_string);

    for (auto& x : js["data"].items()) {
        json j = x.value();

        // Ziskaj clan_id (vzdy je pritomny)
        string clan_id;
        if(j.contains("clan_id")) {
            clan_id = to_string(j["clan_id"].get<int>());
            // Oznac tento clan_id ako videny v API
            processed_clan_ids.insert(clan_id);
        }

        // Kontrola ci klan ma vsetky data - ak nie, je rozpusteny
        if(j.is_null() || !j.contains("tag") || !j.contains("name") ||
           j["tag"].is_null() || j["name"].is_null()) {
            // Klan je rozpusteny - ma len clan_id ale ziadne data
            if(!clan_id.empty()) {
                disbanded_clans.push_back(clan_id);
                cout << "Detekovan rozpusteny klan: " << clan_id << endl;
            }
            continue; // Preskoc tento klan
        }

        ClanData clan;

        clan.clan_id = clan_id;
        clan.tag = j["tag"].get<string>();
        clan.created_at = to_string(j["created_at"].get<int>());
        clan.name = j["name"].get<string>();

        // Escape apostrofy v mene
        clan.EscapeName();

        clan.members_count = to_string(j["members_count"].get<int>());
        clan.emblem195 = j["emblems"]["x195"]["portal"].get<string>();
        clan.emblem64 = j["emblems"]["x64"]["portal"].get<string>();
        clan.emblem32 = j["emblems"]["x32"]["portal"].get<string>();
        clan.emblem24 = j["emblems"]["x24"]["portal"].get<string>();

        clans.push_back(clan);
    }
}

void ClanProcessor::ProcessClan(const ClanData& clan) {
    lock_guard<mutex> lock(batch_mutex);

    auto it = existing_clans.find(clan.clan_id);

    if(it == existing_clans.end()) {
        // Novy klan - pridaj do insert batch
        insert_batch.push_back(clan);
    } else {
        // Existujuci klan - over ci treba update
        if(NeedsUpdate(clan, it->second)) {
            update_batch.push_back(clan);
        }
    }

    // Ak sa naplnil batch, flush
    if(insert_batch.size() >= static_cast<size_t>(BATCH_SIZE)) {
        vector<ClanData> temp_insert = insert_batch;
        insert_batch.clear();
        batch_mutex.unlock();
        BatchInsert(temp_insert);
        batch_mutex.lock();
    }

    if(update_batch.size() >= static_cast<size_t>(BATCH_SIZE)) {
        vector<ClanData> temp_update = update_batch;
        update_batch.clear();
        batch_mutex.unlock();
        BatchUpdate(temp_update);
        batch_mutex.lock();
    }
}

void ClanProcessor::FlushBatches() {
    lock_guard<mutex> lock(batch_mutex);

    if(!insert_batch.empty()) {
        BatchInsert(insert_batch);
        insert_batch.clear();
    }

    if(!update_batch.empty()) {
        BatchUpdate(update_batch);
        update_batch.clear();
    }
}

void ClanProcessor::BatchInsert(const vector<ClanData>& clans) {
    if(clans.empty()) return;

    string insert_query = "INSERT INTO clan_all (clan_id, abbreviation, created_at, name, "
                         "members_count, emblems_large, emblems_medium, emblems_small, emblems_tank) "
                         "VALUES ";

    for(size_t i = 0; i < clans.size(); i++) {
        insert_query += clans[i].ToInsertValues();
        if(i < clans.size() - 1) {
            insert_query += ",";
        }
    }

    PGresult* result = PQexec(conn, insert_query.c_str());

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        cerr << "Chyba batch insert: " << PQresultErrorMessage(result) << endl;
    } else {
        cout << "Batch insert: " << clans.size() << " klanov" << endl;
    }

    PQclear(result);

    // Pridaj do cache
    for(const auto& clan : clans) {
        existing_clans[clan.clan_id] = clan;
    }
}

void ClanProcessor::BatchUpdate(const vector<ClanData>& clans) {
    if(clans.empty()) return;

    string update_query = "UPDATE clan_all as c SET "
                         "abbreviation = u.tag, "
                         "created_at = u.created_at, "
                         "name = u.name, "
                         "members_count = u.members_count, "
                         "emblems_large = u.emblem195, "
                         "emblems_medium = u.emblem64, "
                         "emblems_small = u.emblem32, "
                         "emblems_tank = u.emblem24 "
                         "FROM (VALUES ";

    for(size_t i = 0; i < clans.size(); i++) {
        update_query += clans[i].ToUpdateValues();
        if(i < clans.size() - 1) {
            update_query += ",";
        }
    }

    update_query += ") as u(clan_id, tag, created_at, name, members_count, "
                   "emblem195, emblem64, emblem32, emblem24) "
                   "WHERE c.clan_id = u.clan_id";

    PGresult* result = PQexec(conn, update_query.c_str());

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        cerr << "Chyba batch update: " << PQresultErrorMessage(result) << endl;
    } else {
        cout << "Batch update: " << clans.size() << " klanov" << endl;
    }

    PQclear(result);

    // Aktualizuj cache
    for(const auto& clan : clans) {
        existing_clans[clan.clan_id] = clan;
    }
}

bool ClanProcessor::NeedsUpdate(const ClanData& new_data, const ClanData& old_data) const {
    return (new_data.tag != old_data.tag ||
            new_data.name != old_data.name ||
            new_data.members_count != old_data.members_count ||
            new_data.emblem24 != old_data.emblem24 ||
            new_data.emblem32 != old_data.emblem32 ||
            new_data.emblem64 != old_data.emblem64 ||
            new_data.emblem195 != old_data.emblem195);
}

void ClanProcessor::MarkInactiveClans() {
    vector<string> all_inactive_clans;

    // TYP 1: Rozpustene klany (prisli z API ale bez dat)
    all_inactive_clans.insert(all_inactive_clans.end(),
                              disbanded_clans.begin(),
                              disbanded_clans.end());

    // TYP 2: Klany ktore vobec neprisli z API (uz neexistuju v zozname)
    vector<string> missing_clans;
    for(const auto& [clan_id, clan_data] : existing_clans) {
        if(processed_clan_ids.find(clan_id) == processed_clan_ids.end()) {
            // Tento klan je v DB, ale vobec neprišiel z API
            missing_clans.push_back(clan_id);
        }
    }

    if(!missing_clans.empty()) {
        cout << "Najdenych " << missing_clans.size()
             << " klanov, ktore vobec neprisli z API" << endl;
        all_inactive_clans.insert(all_inactive_clans.end(),
                                 missing_clans.begin(),
                                 missing_clans.end());
    }

    if(all_inactive_clans.empty()) {
        cout << "Ziadne neaktivne/rozpustene klany" << endl;
        return;
    }

    // Vloz vsetky neaktivne klany do tabulky clan_all_empty
    string insert_query = "INSERT INTO clan_all_empty (clan_id) VALUES ";

    for(size_t i = 0; i < all_inactive_clans.size(); i++) {
        insert_query += "(" + all_inactive_clans[i] + ")";
        if(i < all_inactive_clans.size() - 1) {
            insert_query += ",";
        }
    }

    // Pouzi ON CONFLICT aby sa nepridavali duplicity
    insert_query += " ON CONFLICT (clan_id) DO NOTHING";

    PGresult* result = PQexec(conn, insert_query.c_str());

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        cerr << "Chyba pri zapisovani neaktivnych klanov: "
             << PQresultErrorMessage(result) << endl;
    } else {
        cout << "Celkom zapisanych " << all_inactive_clans.size()
             << " neaktivnych klanov do clan_all_empty" << endl;
        cout << "  - Rozpustene (bez dat): " << disbanded_clans.size() << endl;
        cout << "  - Chybaju v API: " << missing_clans.size() << endl;
    }

    PQclear(result);
}

int ClanProcessor::GetInactiveClansCount() const {
    int count = disbanded_clans.size();

    // Pridaj klany ktore vobec neprisli z API
    for(const auto& [clan_id, clan_data] : existing_clans) {
        if(processed_clan_ids.find(clan_id) == processed_clan_ids.end()) {
            count++;
        }
    }

    return count;
}
