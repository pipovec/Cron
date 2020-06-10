#ifndef GETACCOUNTID_H
#define GETACCOUNTID_H

#include <iostream>
#include <libpq-fe.h>
#include <queue>
#include "Pgsql.h"


class GetAccountId
{
    public:
        std::queue<int> dataset();
        int rowCount();
        GetAccountId(std::string query);
        ~GetAccountId();
        int row_count = 0; 

    private:        
        void connect();
        PGresult *result();        
        PGconn *conn;
        Pgsql *pg;
        std::string query;
};

#endif