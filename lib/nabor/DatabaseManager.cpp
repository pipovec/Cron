#include "DatabaseManager.h"
#include <iostream>

DatabaseManager::DatabaseManager(size_t initialSize, size_t maxSize)
    : poolSize(0), maxPoolSize(maxSize)
{
    for (size_t i = 0; i < initialSize; ++i) {
        PGconn* conn = createConnection();
        if (conn) {
            connectionPool.push(conn);
            poolSize++;
        }
    }
}

DatabaseManager::~DatabaseManager()
{
    std::unique_lock<std::mutex> lock(poolMutex);
    while (!connectionPool.empty()) {
        PGconn* conn = connectionPool.front();
        connectionPool.pop();
        PQfinish(conn);
    }
}

PGconn* DatabaseManager::createConnection()
{
    PGconn* conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Chyba pri vytvarani spojenia: "
                  << PQerrorMessage(conn) << std::endl;
        PQfinish(conn);
        return nullptr;
    }

    return conn;
}

PGconn* DatabaseManager::getConnection()
{
    std::unique_lock<std::mutex> lock(poolMutex);

    // Čakaj kým nebude dostupné spojenie
    poolCondition.wait(lock, [this] {
        return !connectionPool.empty() || poolSize < maxPoolSize;
    });

    if (!connectionPool.empty()) {
        PGconn* conn = connectionPool.front();
        connectionPool.pop();

        // Skontroluj či je spojenie stále platné
        if (PQstatus(conn) != CONNECTION_OK) {
            PQfinish(conn);
            poolSize--;
            conn = createConnection();
            if (conn) poolSize++;
        }

        return conn;
    }

    // Vytvor nové spojenie ak pool nie je plný
    if (poolSize < maxPoolSize) {
        PGconn* conn = createConnection();
        if (conn) poolSize++;
        return conn;
    }

    return nullptr;
}

void DatabaseManager::releaseConnection(PGconn* conn)
{
    if (!conn) return;

    std::unique_lock<std::mutex> lock(poolMutex);
    connectionPool.push(conn);
    lock.unlock();
    poolCondition.notify_one();
}

PGresult* DatabaseManager::executePrepared(PGconn* conn, const std::string& stmtName,
                                          int nParams, const char* const* paramValues)
{
    if (!conn) return nullptr;

    PGresult* result = PQexecPrepared(conn, stmtName.c_str(), nParams,
                                     paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(result) != PGRES_TUPLES_OK &&
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba prepared statement: "
                  << PQresultErrorMessage(result) << std::endl;
    }

    return result;
}

PGresult* DatabaseManager::executeQuery(PGconn* conn, const std::string& query)
{
    if (!conn) return nullptr;

    PGresult* result = PQexec(conn, query.c_str());

    if (PQresultStatus(result) != PGRES_TUPLES_OK &&
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba query: " << PQresultErrorMessage(result) << std::endl;
    }

    return result;
}

bool DatabaseManager::prepareStatement(PGconn* conn, const std::string& stmtName,
                                      const std::string& query, int nParams)
{
    if (!conn) return false;

    PGresult* result = PQprepare(conn, stmtName.c_str(), query.c_str(), nParams, nullptr);

    bool success = (PQresultStatus(result) == PGRES_COMMAND_OK);

    if (!success) {
        std::cerr << "Chyba pri priprave statement: "
                  << PQresultErrorMessage(result) << std::endl;
    }

    PQclear(result);
    return success;
}
