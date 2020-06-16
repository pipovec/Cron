#ifndef GETACCOUNTID_H
#define GETACCOUNTID_H

#include <iostream>
#include <libpq-fe.h>
#include <queue>
#include "../Pgsql.h"

/**
 * @hint    Ziska zoznam account_id z databazy z tabulky players_all
 *          a ulozi ich do queue. Jediny parameter do ide priamo
 *          do construtora a je to SQL dotaz.
 *          Public metoda rowCount() vrati pocet vratenych
 *          riadkov.
 */
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