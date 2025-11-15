/*
*   Player Data Collector - Optimalizovaný zber dát o hráčoch
*   Používa prepared statements a batch processing
*
*   author: Boris Fekar (optimized)
*/

#ifndef PLAYERDATACOLLECTOR_H
#define PLAYERDATACOLLECTOR_H

#include "DatabaseManager.h"
#include "BatchProcessor.h"
#include <vector>
#include <string>

class PlayerDataCollector
{
private:
    DatabaseManager* dbManager;
    BatchProcessor* batchProcessor;

    // Pomocné funkcie
    std::string calculateWinrate(const std::string& battles, const std::string& wins);

public:
    PlayerDataCollector(DatabaseManager* manager, BatchProcessor* processor)
        : dbManager(manager), batchProcessor(processor) {}

    // Získaj základné info o hráčoch (optimalizovaný dotaz)
    std::vector<PlayerData> getBasicPlayerInfo();

    // Získaj štatistiky (battles, winrate) pre všetkých hráčov naraz
    bool collectBattleStats(std::vector<PlayerData>& players);

    // Získaj počty tankov (level 6, 8, 10) pre všetkých hráčov
    bool collectVehicleStats(std::vector<PlayerData>& players);

    // Získaj históriu hier (7, 14, 30 dní)
    bool collectHistoryStats(std::vector<PlayerData>& players);

    // Získaj všetky dáta v jednom volaní
    std::vector<PlayerData> collectAllPlayerData();

    // Pomocné - pripravi prepared statements
    bool prepareBulkStatements(PGconn* conn);
};

#endif
