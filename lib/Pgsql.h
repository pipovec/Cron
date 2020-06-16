/*
*   Trieda na pripojenie k databaze clan na server postgresql
*
*   author: Boris Fekar
*/

#ifndef Pgsql_H
#define Pgsql_H
#include <libpq-fe.h>

class Pgsql
{
    private:
            const char *conninfo = "dbname=fpcstat user=deamon password=sedemtri";

    public:
            PGconn *Connect();
            PGconn *pgsql;
            PGconn *Get();
            void Disconnet();
            PGresult *Query(const char *sql);
            Pgsql();
            ~Pgsql();


};

#endif
