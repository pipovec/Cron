#ifndef wn8player_H
#define wn8player_H

#include <iostream>
#include <libpq-fe.h>
#include "./Pgsql.h"
#include <unordered_map>

using namespace std;

class wn8player
{   
    
    struct etv_data
            {
                float frag;
                float dmg;
                float spot;
                float def;
                float win;
            };

    typedef unordered_map<int, etv_data> etv_table;
    etv_table etv; // unordered_map pre etv tabulku
    etv_data  data; // struktura ktora je v  unordered_map pod klucom tank_id 
    
    typedef unordered_map<int,int> mymap;

    public:
            wn8player();
            float GetWN8(int *PlayerData, mymap *TankData);

    private:
            
            float Calculate(int *PlayerData, mymap *TankData);
            void expTable();


};



#endif // wn8player_H