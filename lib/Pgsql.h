/*
*   Trieda na pripojenie k databaze clan na server postgresql
*
*   OPTIMALIZOVANA VERZIA:
*   - Opravene preklepy (Disconnect)
*   - Pridana reconnect logika
*   - Lepsi error handling
*   - Podpora pre prepared statements
*   - Podpora pre transakcie
*   - Connection health check
*
*   author: Boris Fekar
*   optimalizacia: 2025-11-15
*/

#ifndef Pgsql_H
#define Pgsql_H
#include <libpq-fe.h>
#include <string>

class Pgsql
{
    private:
            std::string conninfo;
            bool connected;

            // Interni reconnect
            bool Reconnect();

    public:
            PGconn *pgsql;

            // Zakladne metody
            PGconn *Connect();
            PGconn *Get();
            void Disconnect();  // Opraveny preklep z "Disconnet"

            // Query metody
            PGresult *Query(const char *sql);
            PGresult *QueryParams(const char *sql, int nParams, const char * const *paramValues);

            // Prepared statements
            PGresult *Prepare(const char *stmtName, const char *query, int nParams);
            PGresult *ExecutePrepared(const char *stmtName, int nParams, const char * const *paramValues);

            // Transakcie
            bool BeginTransaction();
            bool CommitTransaction();
            bool RollbackTransaction();

            // Health check
            bool IsConnected();
            bool Ping();

            // Konstruktor a destruktor
            Pgsql();
            ~Pgsql();
};

#endif
