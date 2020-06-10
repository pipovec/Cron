#include "GetAccountId.h"

/**
 * @hint Automaticky vytvory pripojenie k databaze
 * 
*/
GetAccountId::GetAccountId(std::string query)
{
    this->query = query;
    connect();
}

/**
 * @hint Po skonceni, ukonci spojenie k databaze 
 */
GetAccountId::~GetAccountId()
{
    pg->~Pgsql();
}

void GetAccountId::connect()
{    
    pg = new Pgsql;
    conn = pg->Connect();
}

PGresult *GetAccountId::result()
{
    PGresult *result;
    result = PQexec(conn, query.c_str());
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
          { std::cout << "Data z databazy neprisli" <<  PQresultErrorMessage(result) << std::endl;}


    return result;
}

std::queue<int> GetAccountId::dataset()
{
    std::queue<int> ids;
    
    PGresult *res;
    res = this->result();

    row_count = PQntuples(res);

    for(int i = 0; i < row_count; i++)
    {
        ids.push(std::stoi(PQgetvalue(res,i,0)));
        
    }

    PQclear(res);
    
    return ids;
}

int GetAccountId::rowCount()
{
    return row_count;
}