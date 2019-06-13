#include <iostream>
#include <map>
#include <chrono>

/**  Moje kniznice  */
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"


using namespace std;

struct tank {
    int tank_id;
    string name;
    string tag;
    int tier;
    string type;
    string nation;
    bool is_premium;
    string small_icon;
    string contour_icon;
    string big_icon;
};

map<int,tank> tanks;

int page = 1;
int max_page =  2;
int tankov = 0;


void SendPost(int page, nlohmann::json *p_data)
{
    
    const char text[] = "&fields=tank_id,name,tag,tier,type,nation,is_premium,images&language=cs&page_no=";
    string field(text);

    const string method   = "/encyclopedia/vehicles/";
    string post_data = field+to_string(page);
    
    SendCurl send;
   
    string data = send.SendWOT(method, post_data);

    using json = nlohmann::json;
    json js = json::parse(data);
    
    /** Ziskaj kolko stranok ma celkom */
    max_page = js["meta"]["page_total"];
    tankov   = js["meta"]["total"];
    *p_data = js["data"];
}

void StackData(nlohmann::json *p_data)
{
    using json = nlohmann::json;
    json js = *p_data;
    tank tmp;

    for (auto& x : json::iterator_wrapper(js))
    {
        json j = x.value();

        tmp.tank_id = j["tank_id"].get<int>();
        tmp.name = j["name"].get<string>();
        tmp.tag = j["tag"].get<string>();
        tmp.tier = j["tier"].get<int>();
        tmp.type = j["type"].get<string>();
        tmp.nation = j["nation"].get<string>();
        tmp.is_premium = j["is_premium"].get<bool>();
        tmp.small_icon = j["images"]["small_icon"];
        tmp.contour_icon = j["images"]["contour_icon"];
        tmp.big_icon = j["images"]["big_icon"];

        tanks[tmp.tank_id] = tmp;
    }
    
}

void ZiskajData()
{
    using json = nlohmann::json;
    json data, *p_data;
    p_data = &data;

    void StackData(nlohmann::json *p_data);
    void SendPost(int page, json *p_data);

    while(page < max_page)
    {
        SendPost(page, p_data);
        StackData(p_data);
        page ++;
        
    }
}

void UlozData()
{
    map<int,tank>::iterator it;
    string insert  = "INSERT INTO encyclopedia_vehicles (tank_id,name,tag,level,nation,type,is_premium,big_icon,small_icon,contour_icon) VALUES ";
    string data;
    
    for(it = tanks.begin(); it != tanks.end(); it++ ) 
    {
        data += "(" + to_string(it->second.tank_id)+",'"
                    + it->second.name + "','"
                    + it->second.tag + "','"
                    + to_string(it->second.tier) + "','" 
                    + it->second.nation + "','"
                    + it->second.type + "','"
                    + to_string(it->second.is_premium)+ "', '"
                    + it->second.big_icon +"', '"
                    + it->second.small_icon  + "', '"
                    + it->second.contour_icon + "' ),";

    }
    data.pop_back();
    
    string query = insert + data;    
    string truncate = "TRUNCATE encyclopedia_vehicles";
    string vacuum = "VACUUM FULL ANALYZE encyclopedia_vehicles";

    /** Spojenie do databazy */
    PGconn *conn;
    Pgsql *pg = new Pgsql;
    conn = pg->Get();

    /** Vymazanie tabulky */
    PGresult *result;
    result = PQexec(conn, truncate.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba TRUNCATE " <<  PQresultErrorMessage(result) << endl;}
    PQclear(result);

    /** Vlozenie dat */
    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba INSERT " <<  PQresultErrorMessage(result) << endl;}
    PQclear(result);

    /** Vacuum a analyze tabulky */
    result = PQexec(conn, vacuum.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba VACUUM " <<  PQresultErrorMessage(result) << endl;}
    PQclear(result);
            
}


int main()
{
    chrono::time_point<chrono::high_resolution_clock> start, stop;

    start  = chrono::high_resolution_clock::now();
    
    ZiskajData();
    UlozData();

    stop = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed_seconds = stop-start;

    cout << "Celkovy cas \t\t" << elapsed_seconds.count() << endl;
    cout << "Pocet tankov celkom :" << tankov << endl;
    cout << "Pocet stranok celkom :" << max_page << endl;
    return 0;
}