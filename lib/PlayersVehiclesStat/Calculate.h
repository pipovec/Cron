#ifndef CALCULATE_H
#define CALCULATE_H

#include <iostream>
#include <libpq-fe.h>
#include <unordered_map>
#include <queue>
#include <mutex>

#include "../../json/src/json.hpp"
#include "../Pgsql.h"

using namespace std;

/**
 * Vypocet WN8 zo statistik hracovy tankov 
 * 
*/
class Calculate
{
    public:
        Calculate();
        ~Calculate();
        void set(queue<std::string> jsons);

    private:
        void loadEtvTable();
        void playerStatistic();

        struct etv_data
        {
            float frag;
            float dmg;
            float spot;
            float def;
            float win;
        };

        struct playerStat
        {
            int account_id;
            int battles;
            int damage_dealt;
            int dropped_capture_points;
            int frags;
            int spotted;
            int wins;
        };

        typedef unordered_map<int, etv_data> etv_table;

        etv_table etv; // unordered_map pre etv tabulku
        etv_data  data; // struktura ktora je v  unordered_map pod klucom tank_id
        playerStat player_stat; // struktura ktora drzi informacie o hracovych statoch

        mutex queueLock;
        string jsonString;

};

#endif