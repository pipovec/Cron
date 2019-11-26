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
            const char *conninfo = "host=2a03:3b40:100::1:143 dbname=clan user=deamon password=sedemtri";

    public:
            PGconn *Connect();
            PGconn *pgsql;
            PGconn *Get();
            PGresult *Query(const char *sql);
            Pgsql();
            ~Pgsql();


};

#endif
