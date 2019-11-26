#ifndef MEMBERSROLE_H
#define MEMBERSROLE_H

#include <iostream>
#include "./Pgsql.h"
#include "json.hpp"
#include <iostream>

using namespace std;

class MembersRole
{
    public:
        void spracuj(string json);
        void parse_clan_data(int clan_id, nlohmann::json clan_data);
        MembersRole();

    private:
        int meta_count = 0;
        bool chyba = false;
        int clan_id;
        PGconn *connection;

        struct player_struct {
            string account_id;
            string role;
        };

        map<int,player_struct> player;
};

#endif 