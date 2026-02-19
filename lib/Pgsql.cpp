#include "./Pgsql.h"
#include "./DbConfig.h"
#include <libpq-fe.h>
#include <iostream>

Pgsql::Pgsql() : connected(false), pgsql(nullptr)
{
    conninfo = DbConfig::LoadConninfo(
        DbConfig::GetConfigPath(),
        "host=/var/run/postgresql port=5433 dbname=clan user=deamon password=sedemtri"
    );
    this->pgsql = this->Connect();
}

PGconn *Pgsql::Connect()
{
    PGconn *con = PQconnectdb(conninfo.c_str());

    if (PQstatus(con) != CONNECTION_OK) {
        std::cerr << "Chyba pri vytvarani spojenia do databazy!" << std::endl
                  << PQerrorMessage(con) << std::endl;
        connected = false;
        return con; // Vratime connection aj ked je failed, caller moze skontrolovat
    }

    connected = true;
    return con;
}

bool Pgsql::Reconnect()
{
    if (pgsql != nullptr) {
        PQfinish(pgsql);
    }

    pgsql = Connect();
    return connected;
}

PGconn *Pgsql::Get()
{
    // Skontroluj ci je spojenie OK, ak nie, pokus sa reconnect
    if (pgsql == nullptr || PQstatus(pgsql) != CONNECTION_OK) {
        std::cerr << "Connection lost, attempting reconnect..." << std::endl;

        if (!Reconnect()) {
            std::cerr << "Reconnect failed: " << PQerrorMessage(pgsql) << std::endl;
            return nullptr;
        }

        std::cout << "Reconnect successful" << std::endl;
    }

    return pgsql;
}

void Pgsql::Disconnect()
{
    if (pgsql != nullptr) {
        PQfinish(pgsql);
        pgsql = nullptr;
        connected = false;
    }
}

PGresult *Pgsql::Query(const char *sql)
{
    // Skontroluj spojenie pred query
    if (Get() == nullptr) {
        return nullptr;
    }

    PGresult *result = PQexec(pgsql, sql);

    // Kontrola vysledku
    if (result == nullptr) {
        std::cerr << "Query failed: " << PQerrorMessage(pgsql) << std::endl;
    }

    return result;
}

PGresult *Pgsql::QueryParams(const char *sql, int nParams, const char * const *paramValues)
{
    if (Get() == nullptr) {
        return nullptr;
    }

    PGresult *result = PQexecParams(pgsql, sql, nParams, nullptr,
                                    paramValues, nullptr, nullptr, 0);

    if (result == nullptr) {
        std::cerr << "QueryParams failed: " << PQerrorMessage(pgsql) << std::endl;
    }

    return result;
}

PGresult *Pgsql::Prepare(const char *stmtName, const char *query, int nParams)
{
    if (Get() == nullptr) {
        return nullptr;
    }

    PGresult *result = PQprepare(pgsql, stmtName, query, nParams, nullptr);

    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Prepare failed for '" << stmtName << "': "
                  << PQerrorMessage(pgsql) << std::endl;
    }

    return result;
}

PGresult *Pgsql::ExecutePrepared(const char *stmtName, int nParams,
                                  const char * const *paramValues)
{
    if (Get() == nullptr) {
        return nullptr;
    }

    PGresult *result = PQexecPrepared(pgsql, stmtName, nParams,
                                      paramValues, nullptr, nullptr, 0);

    if (result == nullptr) {
        std::cerr << "ExecutePrepared failed for '" << stmtName << "': "
                  << PQerrorMessage(pgsql) << std::endl;
    }

    return result;
}

bool Pgsql::BeginTransaction()
{
    PGresult *result = Query("BEGIN");

    if (result == nullptr || PQresultStatus(result) != PGRES_COMMAND_OK) {
        if (result != nullptr) PQclear(result);
        return false;
    }

    PQclear(result);
    return true;
}

bool Pgsql::CommitTransaction()
{
    PGresult *result = Query("COMMIT");

    if (result == nullptr || PQresultStatus(result) != PGRES_COMMAND_OK) {
        if (result != nullptr) PQclear(result);
        return false;
    }

    PQclear(result);
    return true;
}

bool Pgsql::RollbackTransaction()
{
    PGresult *result = Query("ROLLBACK");

    if (result == nullptr || PQresultStatus(result) != PGRES_COMMAND_OK) {
        if (result != nullptr) PQclear(result);
        return false;
    }

    PQclear(result);
    return true;
}

bool Pgsql::IsConnected()
{
    return pgsql != nullptr && PQstatus(pgsql) == CONNECTION_OK;
}

bool Pgsql::Ping()
{
    if (pgsql == nullptr) {
        return false;
    }

    PGresult *result = PQexec(pgsql, "SELECT 1");

    if (result == nullptr || PQresultStatus(result) != PGRES_TUPLES_OK) {
        if (result != nullptr) PQclear(result);
        return false;
    }

    PQclear(result);
    return true;
}

Pgsql::~Pgsql()
{
    Disconnect(); // Pouzijeme metodu Disconnect() namiesto priameho PQfinish
}
