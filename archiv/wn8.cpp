#include <iostream>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"
#include <libpq-fe.h>
#include <chrono>
#include <ratio>
#include <thread>
#include <unordered_map>

using namespace std;

/*
    Struktura pre tanky a ich hodnoty ktore pojdu do vypoctu wn8
    damage_dealt,spotted,frags,dropped_capture_points,wins,tank_id a musi tam ist aj battles lebo sa robia priemery na jednu bitku
*/
struct tank
{
    int damage_dealt;
    int spotted;
    int frags;
    int dropped_capture_points;
    int wins;
    int battles;
    
};

struct player
{
    int damage_dealt;
    int spotted;
    int frags;
    int dropped_capture_points;
    int wins;
    int battles;

};

/*
    Struktura kam sa ukladaju udaje z ETV tabulky
*/
struct etv_data
{
    float frag;
    float dmg;
    float spot;
    float def;
    float win;
};


// Datove struktury pre ulozenie dat 
typedef unordered_map<int,etv_data> etv_table;
typedef unordered_map<int,tank> player_tanks;

void GetTankData(int account_id, nlohmann::json *tank_data)
{
    /*
        Ziskam data o hracovych tankoch          
    */

    string field  = "&fields=tank_id,all.battles,all.wins,all.spotted,all.frags,all.dropped_capture_points&language=cs";
    const string method   = "/tanks/stats/";

    string post_data = field  + "&account_id="+ to_string(account_id);

    SendCurl send;
    string tmp =  send.SendWOT(method, post_data);

    *tank_data = nlohmann::json::parse(tmp);

    tmp.clear();
}

void GetPlayersData(int account_id, nlohmann::json *player_data)
{
    string field  = "&fields=statistics.all.damage_dealt,statistics.all.spotted,statistics.all.frags,statistics.all.dropped_capture_points,statistics.all.wins,statistics.all.battles,global_rating,client_language&language=cs";
    const string method   = "/account/info/";

    string post_data = field  + "&account_id="+ to_string(account_id);

    SendCurl send;

    string tmp =  send.SendWOT(method, post_data);

    *player_data = nlohmann::json::parse(tmp);
        
    tmp.clear();
}

void GetETVTable(etv_table *pointer)
{
    PGresult *result;
    PGconn *conn;

    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();
    delete pgsql;

    string query    = "SELECT * FROM expected_tank_value";
    result          = PQexec(conn, query.c_str());
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
                {cout << "dotaz na tabulku expected_tank_value je zly: " <<  PQresultErrorMessage(result) << endl;}

    int riadkov     = PQntuples(result);
    int i;
    
    for(i = 0; i < riadkov; i++)
    {
        (*pointer)[stoi(PQgetvalue(result,i,0))].frag = stof(PQgetvalue(result,i,1));
        (*pointer)[stoi(PQgetvalue(result,i,0))].dmg  = stof(PQgetvalue(result,i,2));
        (*pointer)[stoi(PQgetvalue(result,i,0))].spot = stof(PQgetvalue(result,i,3));
        (*pointer)[stoi(PQgetvalue(result,i,0))].def  = stof(PQgetvalue(result,i,4));
        (*pointer)[stoi(PQgetvalue(result,i,0))].win  = stof(PQgetvalue(result,i,5)); // stoi(PQgetvalue(result,i,0));
        
    }
    PQclear(result);
}

int ConvertInput(char *arg[])
{
    string tmp = arg[1];
    int i = stoi(tmp);

    return i;
}


int main(int argv, char *arg[])
{
     
     auto start  = chrono::high_resolution_clock::now();

     etv_table etv; etv_table *p_etv; p_etv = &etv; // unordered_map pre tabulku ETV
     player pl_data;

     int account_id;

     nlohmann::json returned_data; //nlohmann::json *p_returned_data; p_returned_data = &returned_data;
     
     account_id = ConvertInput(arg);
     account_id = 505441546; // zatial RTW001

     {
        nlohmann::json tank_data; nlohmann::json *p_tank_data; p_tank_data = &tank_data;
        nlohmann::json player_data; nlohmann::json *p_player_data; p_player_data = &player_data;
        


        thread T1(GetTankData,account_id, p_tank_data);
        thread T2(GetPlayersData,account_id, p_player_data);
        thread T3(GetETVTable,p_etv);

        T1.join();T2.join();T3.join();

        //cout << player_data << endl;

        cout <<  player_data["data"][to_string(account_id)]<< endl;

        
        // Vytiahnem statusy ci prisli data v poriadku
        returned_data["tank_status"] = tank_data["status"].get<string>();
        returned_data["player_status"] = player_data["status"].get<string>();
        returned_data["global_rating"] = player_data["data"][to_string(account_id)]["global_rating"].get<int>();
        returned_data["client_language"] = player_data["data"][to_string(account_id)]["client_language"].get<string>();

        //Skontrolujem ci je to vsetko "ok"
        string ok  = "ok";
        if(ok.compare(returned_data["tank_status"]) !=0)
            { return returned_data; }
        
        if(ok.compare(returned_data["player_status"]) !=0)
             { return returned_data; }


        // Nahram si do struktury player udaje z json
        pl_data.damage_dealt = player_data["data"][to_string(account_id)]["statistics"]["all"]["damage_dealt"];
        pl_data.spotted = player_data["data"][to_string(account_id)]["statistics"]["all"]["spotted"];
        pl_data.frags = player_data["data"][to_string(account_id)]["statistics"]["all"]["frags"];
        pl_data.dropped_capture_points = player_data["data"][to_string(account_id)]["statistics"]["all"]["dropped_capture_points"];
        pl_data.wins = player_data["data"][to_string(account_id)]["statistics"]["all"]["wins"];
        pl_data.battles = player_data["data"][to_string(account_id)]["statistics"]["all"]["battles"];

        player_data.clear();

    
     }
     
     

    cout << etv[1].frag << endl;

    auto stop  = chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> fp_ms = stop - start;
    
    cout << fp_ms.count() << " - " << pl_data.spotted << endl;
    
    return 0;
}
