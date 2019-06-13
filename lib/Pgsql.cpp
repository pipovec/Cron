#include "./Pgsql.h"
#include <libpq-fe.h>
#include <iostream>

Pgsql::Pgsql()
{
    this->pgsql = this->Connect();
}

PGconn *Pgsql::Connect()
{
    PGconn  *con;

    con = PQconnectdb(conninfo);

    if (PQstatus(con) != CONNECTION_OK)  {
        std::cout << "Chyba pri vytvarani spojenia do databazy!  "<< std::endl << PQerrorMessage(con) << std::endl;
    }

    return con;
}

PGconn *Pgsql::Get()
{
   //PGconn  *con;

    //con = PQconnectdb(conninfo);

    if (PQstatus(this->pgsql) != CONNECTION_OK)  {
        fprintf(stderr, "Connection to database failed: %s",
        PQerrorMessage(this->pgsql));
    }

    return this->pgsql;
}

PGresult *Pgsql::Query(const char *sql)
{

    return PQexec(this->pgsql, sql);
}

Pgsql::~Pgsql()
{
   PQfinish(this->pgsql);
}
