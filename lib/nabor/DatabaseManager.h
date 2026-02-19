/*
*   Thread-safe Database Connection Manager s connection pooling
*
*   author: Boris Fekar (optimized)
*/

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <libpq-fe.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

class DatabaseManager
{
private:
    std::string conninfo;
    std::queue<PGconn*> connectionPool;
    std::mutex poolMutex;
    std::condition_variable poolCondition;
    size_t poolSize;
    size_t maxPoolSize;

    PGconn* createConnection();

public:
    DatabaseManager(size_t initialSize = 3, size_t maxSize = 10);
    ~DatabaseManager();

    // Získaj spojenie z poolu
    PGconn* getConnection();

    // Vráť spojenie späť do poolu
    void releaseConnection(PGconn* conn);

    // Vykonaj dotaz s prepared statement
    PGresult* executePrepared(PGconn* conn, const std::string& stmtName,
                             int nParams, const char* const* paramValues);

    // Vykonaj jednoduchý dotaz
    PGresult* executeQuery(PGconn* conn, const std::string& query);

    // Priprav statement
    bool prepareStatement(PGconn* conn, const std::string& stmtName,
                         const std::string& query, int nParams);
};

// RAII wrapper pre automatické vracanie spojenia
class ConnectionGuard
{
private:
    DatabaseManager* manager;
    PGconn* connection;

public:
    ConnectionGuard(DatabaseManager* mgr) : manager(mgr) {
        connection = manager->getConnection();
    }

    ~ConnectionGuard() {
        if (connection) {
            manager->releaseConnection(connection);
        }
    }

    PGconn* get() { return connection; }

    // Zakáž kopírovanie
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
};

#endif
