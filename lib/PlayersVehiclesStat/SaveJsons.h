#ifndef SAVEJSONS_H
#define SAVEJSONS_H

#include <iostream>
#include <libpq-fe.h>
#include <queue>
#include <mutex>

#include "../Pgsql.h"
#include "../../json/src/json.hpp"

class SaveJsons
{
    public:
        SaveJsons(std::queue<std::string> jsons);
        ~SaveJsons();
        void Parse(std::queue<std::string> jsons);

    private:
        void SaveToDatabase(std::string account_id, std::string value);
        void Connection();
        void clearQueue(std::queue<std::string> &q);
        std::string table = "players_vehicles_stat_json";
        PGconn *conn;
        Pgsql *pgsql;
};

#endif