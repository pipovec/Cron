
#include "./GetPlayersStats.h"
#include "./SendCurl.h"
#include "../json/src/json.hpp"

#include <iostream>
#include <unordered_map>

using namespace std;


void GetPlayersStats::Gets(string account_ids, PlayersStats *playerStat)
{
    string post_data = field +"&account_id="+account_ids;

    SendCurl *send = new SendCurl; 
    string json_data = send->SendWOT(method, post_data); 
    delete send;
    

    using json = nlohmann::json;
    json js;
    json udaje, j;

    try {
        js = json::parse(json_data);
    }
    catch(json::parse_error& e)
    {
        cout << "Parser 0: " << e.what() << endl;        
    }
    
    try {
        udaje = js["data"];
    }
    catch(json::parse_error& e)
    {
        cout << "Parser 1: " << e.what() << endl;        
    }

    wn_data data;

    for (auto& x : json::iterator_wrapper(udaje))
        {
            int account_id;       
            int battles,dcp,dmg,frags,spot,wins;

            account_id = stoi(x.key());
            
            if(x.value() != NULL)
            {
                try {
                    j = x.value();                
                }
                catch(json::parse_error& e)
                {
                    cout << "Parser : " << e.what() << endl;                    
                }
                
                
                if( !j.is_null() )
                {                    
                
                    battles = j["statistics"]["all"]["battles"];
                    dcp     = j["statistics"]["all"]["dropped_capture_points"];
                    dmg     = j["statistics"]["all"]["damage_dealt"];
                    frags   = j["statistics"]["all"]["frags"];
                    spot    = j["statistics"]["all"]["spotted"];
                    wins    = j["statistics"]["all"]["wins"];
                                                        

                    if(dcp > 0) {
                        data.dcp = dcp;
                    }
                    else {
                        data.dcp = 0.0;
                    }

                    if(dmg > 0) {
                        data.dmg = dmg;
                    }else {
                        data.dmg = 0.0;
                    }

                    if(frags > 0) {
                        data.frags = frags;
                    }else {
                        data.frags = 0.0;
                    }

                    if(spot > 0) {
                        data.spot = spot;
                    }else {
                        data.spot = 0.0;
                    }

                    if(wins > 0) {
                        data.wins = frags;
                    }else {
                        data.wins = 0.0;
                    }
                    
                    if(battles > 0) {
                        data.battles = (battles*1.0);
                    }
                    
                   (*playerStat)[account_id] = data;
                   data.wins = data.spot = data.frags = data.dmg = data.dcp = data.battles = 0;
                   
                } 

                j = NULL;
                
            } 
            
        }   
    udaje = NULL; js = NULL; json_data.clear(); post_data.clear();
}