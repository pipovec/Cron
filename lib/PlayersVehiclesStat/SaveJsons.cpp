#include "SaveJsons.h"

SaveJsons::SaveJsons(std::queue<std::string> jsons)
{
    SaveJsons::Connection();
    this->Parse(jsons);
}

void SaveJsons::Parse(std::queue<std::string> jsons)
{
    using json = nlohmann::json;
    json js;

    while (jsons.size() > 0)
    {
        std::string json = jsons.front();
        jsons.pop();

        try
        {
            js = json::parse(json);

            for (auto& [key, val] : js["data"].items())  
            {  
                //std::cout << "key: " << key <<  '\n';
                this->SaveToDatabase(key, val.dump());
            }
        }
        catch (json::parse_error &e)
        {
            std::cout << "Chyba parsovania: " << e.what() << std::endl;
        }
        
        js.clear();
    }

    SaveJsons::clearQueue(jsons);
}

void SaveJsons::Connection()
{
    pgsql = new Pgsql;
    conn = pgsql->Get();
}

void SaveJsons::clearQueue(std::queue<std::string> &q)
{
    std::queue<std::string> empty;
    std::swap(q, empty);
}

void SaveJsons::SaveToDatabase(std::string account_id, std::string value)
{
    PGresult *result;
    std::string query = "INSERT INTO players_vehicles_stat_json (account_id, json_data) VALUES (" + account_id + ",'" + value + "')" ;
    
    result = PQexec(conn, query.c_str());    

    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        std::cout << "Chyba insertu! "  <<  PQresultErrorMessage(result) << std::endl;
        PQclear(result);
    }

    PQclear(result);
}

SaveJsons::~SaveJsons()
{    
    pgsql->~Pgsql();
}