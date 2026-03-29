
#include "./GetPlayersVehicles.h"
#include "./SendCurl.h"
#include "../json/src/json.hpp"

#include <iostream>
#include <unordered_map>

using namespace std;


void GetPlayersVehicles::Gets(string account_ids, Data_vehicle *p_vehicles)
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

    
    int account_id;

    for (auto& x : udaje.items())
    {
        vehicle_battles data;
        
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
                for (auto& y : j.items())
                {
                    auto v = y.value();
                    //cout << v["tank_id"]  <<" - " << v["statistics"]["battles"] << endl;
                    data[ v["tank_id"] ] = v["statistics"]["battles"];
                    v=NULL;
                }
                                    
                (*p_vehicles)[account_id] = data;
                            
            } 

            j = NULL;
            data.clear();
        } 
    }   
    udaje = NULL; js = NULL; json_data.clear(); post_data.clear();
}
