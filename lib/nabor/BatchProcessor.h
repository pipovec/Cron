/*
*   Batch Processor pre efektívne spracovanie SQL operácií v dávkach
*
*   author: Boris Fekar (optimized)
*/

#ifndef BATCHPROCESSOR_H
#define BATCHPROCESSOR_H

#include "DatabaseManager.h"
#include <string>
#include <vector>
#include <sstream>

class BatchProcessor
{
private:
    DatabaseManager* dbManager;
    size_t batchSize;

public:
    BatchProcessor(DatabaseManager* manager, size_t batch = 1000)
        : dbManager(manager), batchSize(batch) {}

    // Batch INSERT operácie
    bool batchInsert(const std::string& tableName,
                     const std::vector<std::string>& columns,
                     const std::vector<std::vector<std::string>>& rows);

    // Batch UPDATE pomocou temporary table
    bool batchUpdate(const std::string& tableName,
                     const std::string& keyColumn,
                     const std::vector<std::string>& updateColumns,
                     const std::vector<std::vector<std::string>>& rows);

    // Pomocná funkcia pre escapovanie stringu
    static std::string escapeString(const std::string& input);

    // Pomocná funkcia pre vytvorenie VALUES časti INSERT dotazu
    static std::string buildValuesString(const std::vector<std::vector<std::string>>& rows,
                                        size_t start, size_t end);
};

// Struktura pre player data
struct PlayerData {
    std::string account_id;
    std::string nickname;
    std::string global_rating;
    std::string wn8;
    std::string battles_all;
    std::string winrate;
    std::string last7;
    std::string last14;
    std::string last30;
    std::string avgxp;
    std::string pocet_x;
    std::string pocet_8;
    std::string pocet_6;
};

#endif
