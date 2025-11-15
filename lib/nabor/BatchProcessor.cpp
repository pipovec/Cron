#include "BatchProcessor.h"
#include <iostream>
#include <algorithm>

std::string BatchProcessor::escapeString(const std::string& input)
{
    std::string result;
    result.reserve(input.length() * 2);

    for (char c : input) {
        if (c == '\'') {
            result += "''";
        } else if (c == '\\') {
            result += "\\\\";
        } else {
            result += c;
        }
    }

    return result;
}

std::string BatchProcessor::buildValuesString(const std::vector<std::vector<std::string>>& rows,
                                             size_t start, size_t end)
{
    std::ostringstream oss;

    for (size_t i = start; i < end && i < rows.size(); ++i) {
        if (i > start) oss << ",";

        oss << "(";
        for (size_t j = 0; j < rows[i].size(); ++j) {
            if (j > 0) oss << ",";

            const std::string& value = rows[i][j];

            // Ak je hodnota číselná alebo NULL, nepridávaj úvodzovky
            if (value == "NULL" || value.empty()) {
                oss << "NULL";
            } else if (std::all_of(value.begin(), value.end(),
                                  [](char c) { return std::isdigit(c) || c == '.' || c == '-'; })) {
                oss << value;
            } else {
                oss << "'" << escapeString(value) << "'";
            }
        }
        oss << ")";
    }

    return oss.str();
}

bool BatchProcessor::batchInsert(const std::string& tableName,
                                 const std::vector<std::string>& columns,
                                 const std::vector<std::vector<std::string>>& rows)
{
    if (rows.empty()) return true;

    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) {
        std::cerr << "Nepodarilo sa ziskat spojenie pre batch insert" << std::endl;
        return false;
    }

    // Vytvor INSERT dotaz po dávkach
    size_t totalRows = rows.size();
    size_t processed = 0;

    while (processed < totalRows) {
        size_t end = std::min(processed + batchSize, totalRows);

        std::ostringstream query;
        query << "INSERT INTO " << tableName << " (";

        // Columns
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) query << ",";
            query << columns[i];
        }

        query << ") VALUES ";
        query << buildValuesString(rows, processed, end);

        // Vykonaj dotaz
        PGresult* result = dbManager->executeQuery(conn, query.str());

        if (!result || PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::cerr << "Chyba batch insert: "
                      << (result ? PQresultErrorMessage(result) : "NULL result")
                      << std::endl;
            if (result) PQclear(result);
            return false;
        }

        PQclear(result);
        processed = end;
    }

    return true;
}

bool BatchProcessor::batchUpdate(const std::string& tableName,
                                 const std::string& keyColumn,
                                 const std::vector<std::string>& updateColumns,
                                 const std::vector<std::vector<std::string>>& rows)
{
    if (rows.empty()) return true;

    ConnectionGuard guard(dbManager);
    PGconn* conn = guard.get();

    if (!conn) {
        std::cerr << "Nepodarilo sa ziskat spojenie pre batch update" << std::endl;
        return false;
    }

    // Vytvor temporary table
    std::string tempTable = "temp_" + tableName + "_update";

    std::ostringstream createTemp;
    createTemp << "CREATE TEMPORARY TABLE " << tempTable << " AS SELECT * FROM "
               << tableName << " LIMIT 0";

    PGresult* result = dbManager->executeQuery(conn, createTemp.str());
    if (!result || PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba vytvorenia temp table: "
                  << (result ? PQresultErrorMessage(result) : "NULL") << std::endl;
        if (result) PQclear(result);
        return false;
    }
    PQclear(result);

    // Vlož dáta do temp table
    std::vector<std::string> allColumns = {keyColumn};
    allColumns.insert(allColumns.end(), updateColumns.begin(), updateColumns.end());

    if (!batchInsert(tempTable, allColumns, rows)) {
        return false;
    }

    // UPDATE pomocou JOIN s temp table
    std::ostringstream updateQuery;
    updateQuery << "UPDATE " << tableName << " SET ";

    for (size_t i = 0; i < updateColumns.size(); ++i) {
        if (i > 0) updateQuery << ", ";
        updateQuery << updateColumns[i] << " = " << tempTable << "." << updateColumns[i];
    }

    updateQuery << " FROM " << tempTable
                << " WHERE " << tableName << "." << keyColumn
                << " = " << tempTable << "." << keyColumn;

    result = dbManager->executeQuery(conn, updateQuery.str());

    bool success = (result && PQresultStatus(result) == PGRES_COMMAND_OK);

    if (!success) {
        std::cerr << "Chyba batch update: "
                  << (result ? PQresultErrorMessage(result) : "NULL") << std::endl;
    }

    if (result) PQclear(result);

    // Vymaž temp table
    std::string dropTemp = "DROP TABLE " + tempTable;
    result = dbManager->executeQuery(conn, dropTemp);
    if (result) PQclear(result);

    return success;
}
