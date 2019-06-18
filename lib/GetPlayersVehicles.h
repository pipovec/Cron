#ifndef GETPLAYERSVEHICLES_H
#define GETPLAYERSVEHICLES_H


#include "./SendCurl.h"
#include <unordered_map>
#include <iostream>

using namespace std;

/**
 * Ziskanie statistik tankov pre PlayersWN8v2
 */


/* Container na vozidla jednoho hraca */
typedef unordered_map<int, int> vehicle_battles;

/* Container na hracske staty */
typedef unordered_map<int, vehicle_battles> Data_vehicle;


class GetPlayersVehicles {


public:
    void Gets(string account_ids, Data_vehicle *p_vehicles);


private:
    const string field = "&fields=statistics.battles,tank_id";
    const string method   = "/account/tanks/";

};

#endif  