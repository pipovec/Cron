#ifndef GETPLAYERSSTATS_H
#define GETPLAYERSSTATS_H



#include "./GetPlayersStats.h"
#include "./SendCurl.h"
#include <unordered_map>
#include <iostream>

using namespace std;

/**
 * Ziskanie hracskych statistik pre PlayersWN8v2
 */

struct wn_data {
    int battles;
    float wins;
    float spot;
    float dmg;
    float frags;
    float dcp;
};

/* Container na hracske staty */
typedef unordered_map<int,wn_data> PlayersStats;

class GetPlayersStats {


public:
    void Gets(string account_ids, PlayersStats *playerStat);


private:
    // Jeden SendCurl na objekt = jedno znovupouzivane spojenie (keep-alive).
    // Kazde vlakno ma vlastny objekt, preto nie je zdielany medzi vlaknami.
    SendCurl send_;

    const string field = "&fields=statistics.all.spotted,statistics.all.frags,statistics.all.battles,statistics.all.damage_dealt,statistics.all.dropped_capture_points,statistics.all.wins";
    const string method   = "/account/info/";

};

#endif  