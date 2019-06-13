/*
*   Program na spracovanie podrobnych statistik hracovych tankov.
*   Treba ho spustit az po programe PlayersStat, lebo vyberie iba
*   tie account_id ktore vcera odohrali aspon jednu bitku.
*
*/

#include <iostream>
#include <map>
#include <chrono>
#include <mutex>
#include <thread>
#include <fstream>
#include <fstream>

/** Kontainer **/
#include <queue>
#include <map>

/**  Moje kniznice  */
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"



/** Co budem pozadovat zo serveru */
string field = "&fields=stronghold_skirmish.battles,stronghold_skirmish.wins,stronghold_skirmish.damage_dealt,stronghold_skirmish.spotted,stronghold_skirmish.frags,stronghold_skirmish.dropped_capture_points,"
"globalmap.battles,globalmap.wins,globalmap.damage_dealt,globalmap.spotted,globalmap.frags,globalmap.dropped_capture_points,"
"stronghold_defense.battles,stronghold_defense.wins,stronghold_defense.damage_dealt,stronghold_defense.spotted,stronghold_defense.frags,stronghold_defense.dropped_capture_points,"
"all.battles,all.wins,all.damage_dealt,all.spotted,all.frags,all.dropped_capture_points,tank_id";

const string method   = "/tanks/stats/";

PGconn *conn;
int riadkov;

/** Kontajner na account_id */
queue<int> account_id;

/** Kontajner na ziskane jsony */
map<int,string> json_map;

/** Struktura na data z databazy */ 
struct tank_data {
    int battles;
    int wins;
    int spot;
    int dmg;
    int frags;
    int dcp;
};
map<int,tank_data> all;
map<int,tank_data> skirmish;
map<int,tank_data> defense;
map<int,tank_data> globalmap;

/** Pocitadla pre dotazy */
int insert_all_count = 0; int insert_all_h_count = 0;
int insert_skirmish_count = 0; int insert_skirmish_h_count = 0;
int insert_defense_count = 0;   int insert_defense_h_count = 0;
int insert_map_count = 0; int insert_map_h_count = 0;

int counter[] = {0,0,0,0,0,0,0,0};
int fails = 0;

int player_counter; 

/** Zamok pre kriticku oblast json */
mutex id_lock;
mutex json_lock;

/** Vytvor jedno spojenie do databazy */
void VytvorSpojenie()
{
    Pgsql *pg = new Pgsql;
    conn = pg->Get();
}

void VacuumAnalyze()
{
    string query = "VACUUM ANALYZE pvs_all_01";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_02";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_03";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_04";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_05";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_05";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_06";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_07";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_08";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_09";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_all_10";
        PQsendQuery(conn,query.c_str());


    query = "VACUUM ANALYZE pvs_skirmish";
        PQsendQuery(conn,query.c_str());        
    query = "VACUUM ANALYZE pvs_skirmish_history";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_defense";
        PQsendQuery(conn,query.c_str());
    query = "VACUUM ANALYZE pvs_defense_history";
        PQsendQuery(conn,query.c_str());    
        
    cout << "Prebehlo vacuum a analyze " << endl << endl;
}

/** Nahraj account_id do fronty */
void GetAccountId() 
{
        
    PGresult *result;
    //const char *query = "SELECT account_id FROM ps_all_history WHERE date = current_date - interval '1 days ' AND battles > 0";
    const char *query  = "SELECT account_id FROM players_all";
    result          = PQexec(conn, query);
         if (PQresultStatus(result) != PGRES_TUPLES_OK)
                {cout << "GetPlayers: " <<  PQresultErrorMessage(result) << endl;}

    riadkov     = PQntuples(result);
    player_counter = riadkov;
    for(int i = 0; i < riadkov; i++)
    {
        account_id.push(stoi(PQgetvalue(result,i,0)));
        
    }
    PQclear(result);
}
void UlozSubor(string file_name, const char *json_string)
{
    ofstream file;
    file.open ("./tmp/"+file_name, ios::trunc | ios::out);
    file << json_string;
    file.close();
}

string CitajSubor(int i)
{
    string data;    
    ifstream file;
    string filename = "./tmp/pvs_"+ to_string(i) + ".json";
    file.open(filename, ios::in);

    if(file.is_open())
    {
       file >> data;
    }
    
    return data;
}

/** Ziskaj data zo servera a uloz ich do docasneho suboru */
void GetDataFromSever(int number)
{       
    string json_string;
    int x = 1;
    
    chrono::seconds dura(3); // pausa 3 sec
    
    if(account_id.size() > 0)
    {
        id_lock.lock();
            int id = account_id.front();
            account_id.pop();            
        id_lock.unlock();

        string post_data = field+"&account_id="+to_string(id);
       
        

        SendCurl send;
        do{
            try {
                json_string = send.SendWOT(method, post_data);
                x = 0;
                player_counter -- ;
            }
            catch(exception& e) {
                cout << e.what() << endl;
                this_thread::sleep_for( dura );
                x = 1;
                //json = send.SendWOT(method, post_data);
            }
        }
        while(x != 0);
        
        /** Ulozenie json do docasneho suboru */
        string file_name = "pvs_"+to_string(number)+".json";
        const char *data = json_string.c_str();
        UlozSubor(file_name, data);
        json_string.clear();
    }
}

/** Ziskanie surovych dat a ulozenie do suboru, 15 hracov naraz */
void GetJson()
{
        
        thread t1(GetDataFromSever,1);
        thread t2(GetDataFromSever,2);
        thread t3(GetDataFromSever,3);
        thread t4(GetDataFromSever,4);
        thread t5(GetDataFromSever,5);
        thread t6(GetDataFromSever,6);
        thread t7(GetDataFromSever,7);
        thread t8(GetDataFromSever,8);
        thread t9(GetDataFromSever,9);
        thread t10(GetDataFromSever,10);
        thread t11(GetDataFromSever,11);
        thread t12(GetDataFromSever,12);
        thread t13(GetDataFromSever,13);
        thread t14(GetDataFromSever,14);
        thread t15(GetDataFromSever,15);
        t1.join();t2.join();t3.join();t4.join();t5.join();t6.join();t7.join();t8.join();t9.join();t10.join();t11.join();t12.join();t13.join();t14.join();t15.join();
        
}

PGresult *Database(int id, string table)
{
    PGresult *result;

    string query    = "SELECT * FROM "+table+" WHERE account_id= "+to_string(id) ;
    result          = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
          {cout << "Data z tabulku "+table+" neprisli" <<  PQresultErrorMessage(result) << endl;}
    
    return result;
}

void GetDatabaseData(string json_data)
{
    PGresult *Database(int id, string table);
    void UrobPvsAll(string json1, string *i, string *ih, string *u);
    void UrobPvsskirmish(string json2, string *i, string *ih, string *u);
    void UrobPvsdefense(string json3, string *i, string *ih, string *u);
    void UrobPvsMap(string json4, string *i, string *ih, string *u);

    void InsertInTable(string *data, string table);
    void UpdateTable(string *data, string table);
    
    PGresult *result;
    tank_data tdata;
    
    /** Dotazy do tabuliek pvs_all Random */
    string insert_pvs_all=""; string *ipa; ipa = &insert_pvs_all;
    string insert_pvs_all_history = ""; string *ipah; ipah = &insert_pvs_all_history;
    string update_pvs_all = ""; string *upa; upa = &update_pvs_all; 

    string insert_pvs_skirmish=""; string *ips; ips = &insert_pvs_skirmish;
    string insert_pvs_skirmish_history = ""; string *ipsh; ipsh = &insert_pvs_skirmish_history;
    string update_pvs_skirmish = ""; string *ups; ups = &update_pvs_skirmish;
    
    string insert_pvs_defense=""; string *ipd; ipd = &insert_pvs_defense;
    string insert_pvs_defense_history = ""; string *ipdh; ipdh = &insert_pvs_defense_history;
    string update_pvs_defense = ""; string *upd; upd = &update_pvs_defense;

    string insert_pvs_globalmap=""; string *ipm; ipm = &insert_pvs_globalmap;
    string insert_pvs_globalmap_history = ""; string *ipmh; ipmh = &insert_pvs_globalmap_history;
    string update_pvs_globalmap = ""; string *upm; upm = &update_pvs_globalmap; 

    int id,riadkov,i;

    using json = nlohmann::json;
    json js,j;
  

    try {
        js = json::parse(json_data);
    }
    catch(json::parse_error& e)
    {
        cout << "Parser 2: " << e.what() << endl;
        fails ++;
    }

    js = js["data"];
    
    /** Ziskanie account_id */
    for (auto& x : json::iterator_wrapper(js))
    {
        id = stoi(x.key());
    } 
    js.clear();
        
    /** Ziskanie a ulozenie data z databazy */
    
    /** Tabulka pvs_all */
    result = Database(id, "pvs_all");
    riadkov = PQntuples(result);

    for(i = 0; i < riadkov; i++) {
        tdata.battles   = stoi(PQgetvalue(result,i,2));
        tdata.wins      = stoi(PQgetvalue(result,i,3));
        tdata.spot      = stoi(PQgetvalue(result,i,4));
        tdata.dmg       = stoi(PQgetvalue(result,i,5));
        tdata.frags     = stoi(PQgetvalue(result,i,6));
        tdata.dcp       = stoi(PQgetvalue(result,i,7));

        all[stoi(PQgetvalue(result,i,1))]  = tdata;
        
    }
    PQclear(result);

    /** Tabulka pvs_skirmish */
    result = Database(id, "pvs_skirmish");
    riadkov = PQntuples(result);

    for(i = 0; i < riadkov; i++) {
        tdata.battles   = stoi(PQgetvalue(result,i,2));
        tdata.wins      = stoi(PQgetvalue(result,i,3));
        tdata.spot      = stoi(PQgetvalue(result,i,4));
        tdata.dmg       = stoi(PQgetvalue(result,i,5));
        tdata.frags     = stoi(PQgetvalue(result,i,6));
        tdata.dcp       = stoi(PQgetvalue(result,i,7));

        skirmish[stoi(PQgetvalue(result,i,1))]  = tdata;
        
    }
    PQclear(result);

    /** Tabulka pvs_defense */
    result = Database(id, "pvs_defense");
    riadkov = PQntuples(result);

    for(i = 0; i < riadkov; i++) {
        tdata.battles   = stoi(PQgetvalue(result,i,2));
        tdata.wins      = stoi(PQgetvalue(result,i,3));
        tdata.spot      = stoi(PQgetvalue(result,i,4));
        tdata.dmg       = stoi(PQgetvalue(result,i,5));
        tdata.frags     = stoi(PQgetvalue(result,i,6));
        tdata.dcp       = stoi(PQgetvalue(result,i,7));

        defense[stoi(PQgetvalue(result,i,1))]  = tdata;
        
    }
        PQclear(result);

        /** Tabulka pvs_globalmap */
        result = Database(id, "pvs_globalmap");
        riadkov = PQntuples(result);

        for(i = 0; i < riadkov; i++) {
        tdata.battles   = stoi(PQgetvalue(result,i,2));
        tdata.wins      = stoi(PQgetvalue(result,i,3));
        tdata.spot      = stoi(PQgetvalue(result,i,4));
        tdata.dmg       = stoi(PQgetvalue(result,i,5));
        tdata.frags     = stoi(PQgetvalue(result,i,6));
        tdata.dcp       = stoi(PQgetvalue(result,i,7));

        globalmap[stoi(PQgetvalue(result,i,1))]  = tdata;
            
        }
        PQclear(result);
        
        

        UrobPvsAll(json_data, ipa, ipah, upa);
        UrobPvsskirmish(json_data, ips, ipsh, ups);
        UrobPvsdefense(json_data, ipd, ipdh, upd);
        UrobPvsMap(json_data, ipm, ipmh, upm);
       
        all.clear();skirmish.clear();globalmap.clear();defense.clear();
        json_data.clear();
    
   
    /** Odosielanie pripravenych dotazov */
    
    /** INSERT */
    if(insert_all_count > 0) {
        InsertInTable(ipa,"pvs_all");
    }
    if(insert_all_h_count > 0) {
        InsertInTable(ipah,"pvs_all_history");
        UpdateTable(upa,"pvs_all");
    }
    if(insert_skirmish_count > 0) {
        InsertInTable(ips,"pvs_skirmish");
    }
    if(insert_skirmish_h_count > 0) {
        InsertInTable(ipsh,"pvs_skirmish_history");
        UpdateTable(ups,"pvs_skirmish");
    }
    if(insert_defense_count > 0) {
        InsertInTable(ipd,"pvs_defense");
    }
    if(insert_defense_h_count > 0) {
        InsertInTable(ipdh,"pvs_defense_history");
        UpdateTable(upd,"pvs_defense");
    }
    if(insert_map_count > 0) {
        InsertInTable(ipm,"pvs_globalmap");
    }
    if(insert_map_h_count > 0) {
        InsertInTable(ipmh,"pvs_globalmap_history");
        UpdateTable(upm,"pvs_all");
    }

    counter[0]  += insert_all_count ;
    counter[1]  += insert_all_h_count;
    counter[2]  += insert_skirmish_count;
    counter[3]  += insert_skirmish_h_count;
    counter[4]  += insert_defense_count;
    counter[5]  += insert_defense_h_count;
    counter[6]  += insert_map_count;
    counter[7]  += insert_map_h_count;

    /** Mazanie pocitadiel */
    insert_all_count = insert_all_h_count = insert_skirmish_count = insert_skirmish_h_count = 0;
    insert_defense_count = insert_defense_h_count = insert_map_count = insert_map_h_count = 0;

    /** Mazanie stringov */
    insert_pvs_all.clear();insert_pvs_all_history.clear(); update_pvs_all.clear();
    insert_pvs_skirmish.clear();insert_pvs_skirmish_history.clear();update_pvs_skirmish.clear();
    insert_pvs_defense.clear();insert_pvs_defense_history.clear();update_pvs_defense.clear();
    insert_pvs_globalmap.clear();insert_pvs_globalmap_history.clear();update_pvs_globalmap.clear();

    /** Mazanie map */
    all.clear();skirmish.clear();defense.clear();globalmap.clear();

}

void InsertInTable(string *data, string table)
{
    string values = *data;
    values.pop_back();
    PGresult *result;

    string query = "INSERT INTO "+table+" VALUES " + values ;
    
    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {cout << "Chyba insertu do tabulky " + table <<  PQresultErrorMessage(result) << endl;}

    PQclear(result);

}

void UpdateTable(string *data, string table)
{
    string values = *data;
    values.pop_back();
    PGresult *result;

    string query = "UPDATE "+table+" as p SET battles = u.battles, wins = u.wins, spotted = u.spotted, damage_dealt = u.damage_dealt, frags = u.frags, dropped_capture_points = u.dropped_capture_points FROM (VALUES " + values + ") as u(account_id,tank_id,battles,wins,spotted,damage_dealt,frags,dropped_capture_points) WHERE p.account_id = u.account_id AND p.tank_id = u.tank_id";

    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {cout << "Chyba update tabulky "+table <<  PQresultErrorMessage(result) << endl;}

    PQclear(result);
}


void UrobPvsAll(string json_data,string *insert_pvs_all, string *insert_pvs_all_history, string *update_pvs_all)
{
       
    using json = nlohmann::json;
    
    json js,j,c;
    string account_id;
    try {
        js = json::parse(json_data); //json_data.clear();        
    }
    catch(json::parse_error& e) {
        cout << "Parser UrobPvsAll: " << e.what() << endl;
        fails ++;
    }

    js = js["data"];
    
    for (auto& x : json::iterator_wrapper(js))
    {
        account_id = x.key();
        j = x.value();       
        
    }  
          
    for (auto& y : json::iterator_wrapper(j))
    {
          c = y.value();  
          int tank_id = c["tank_id"].get<int>();
          
          if(all.count(tank_id) > 0 ) // ak najde taky tank_id v kontajnery
          {
            
            if(all[tank_id].battles != c["all"]["battles"].get<int>() ) // Ak su rozdielne bitky tak to zaznamenaj
            {
                /** Vloz data do databazy do pvs_all_history */
                *insert_pvs_all_history +=  "("  +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["all"]["battles"].get<int>() - all[tank_id].battles)+ ","
                                                + to_string(c["all"]["wins"].get<int>() - all[tank_id].wins  )+ ","
                                                + "current_date - interval '1 days',"
                                                + to_string(c["all"]["spotted"].get<int>()  - all[tank_id].spot )+ ","
                                                + to_string(c["all"]["damage_dealt"].get<int>() - all[tank_id].dmg )+ ","
                                                + to_string(c["all"]["frags"].get<int>() - all[tank_id].frags )+ ","
                                                + to_string(c["all"]["dropped_capture_points"].get<int>() - all[tank_id].dcp )+ "),";  
                
                /** A update data v databaze v pvs_all */
                *update_pvs_all += "("           +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["all"]["battles"].get<int>())+ ","
                                                + to_string(c["all"]["wins"].get<int>())+ ","
                                                + to_string(c["all"]["spotted"].get<int>())+ ","
                                                + to_string(c["all"]["damage_dealt"].get<int>())+ ","
                                                + to_string(c["all"]["frags"].get<int>())+ ","
                                                + to_string(c["all"]["dropped_capture_points"].get<int>())+ "),";

                insert_all_h_count ++; /** update pvs_all a insertov do pvs_all_historie musi by rovnako, tak ich nemusim pocitat zvlast */
            }      
          }
          else // ak nenajde tanky tak ich da na pridanie
          {
            *insert_pvs_all += "("+account_id+"," 
                                    + to_string(c["tank_id"].get<int>()) +","
                                    + to_string(c["all"]["battles"].get<int>())+ ","
                                    + to_string(c["all"]["wins"].get<int>())+ ","
                                    + to_string(c["all"]["spotted"].get<int>())+ ","
                                    + to_string(c["all"]["damage_dealt"].get<int>())+ ","
                                    + to_string(c["all"]["frags"].get<int>())+ ","
                                    + to_string(c["all"]["dropped_capture_points"].get<int>())+ "),";         
            insert_all_count ++;
          }
          
    }
    
    js.clear(); j.clear(); c.clear(); json_data.clear();  
}

void UrobPvsskirmish(string json_data,string *insert_pvs_skirmish, string *insert_pvs_skirmish_history, string *update_pvs_skirmish)
{
       
    using json = nlohmann::json;
    json js,j,c;
    string account_id;
    
    try {        
        js = json::parse(json_data);      
    }
    catch(json::parse_error& e) {
        cout << "Parser UrobPvsSkirmish: " << e.what() << endl;
        fails ++ ;
    }

    js      = js["data"];
    
    for (auto& x : json::iterator_wrapper(js))
    {
        account_id = x.key();
        j = x.value();
    }   
        
    for (auto& y : json::iterator_wrapper(j))
    {
          c = y.value();  
          int tank_id = c["tank_id"].get<int>();
          
          if(skirmish.count(tank_id) > 0 ) // ak najde taky tank_id v kontajnery
          {
            
            if(skirmish[tank_id].battles != c["stronghold_skirmish"]["battles"].get<int>() ) // Ak su rozdielne bitky tak to zaznamenaj
            {
                /** Vloz data do databazy do pvs_skirmish_history */
                *insert_pvs_skirmish_history +=  "("  +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["stronghold_skirmish"]["battles"].get<int>() - skirmish[tank_id].battles)+ ","
                                                + to_string(c["stronghold_skirmish"]["wins"].get<int>() - skirmish[tank_id].wins  )+ ","
                                                + "current_date - interval '1 days',"
                                                + to_string(c["stronghold_skirmish"]["spotted"].get<int>()  - skirmish[tank_id].spot )+ ","
                                                + to_string(c["stronghold_skirmish"]["damage_dealt"].get<int>() - skirmish[tank_id].dmg )+ ","
                                                + to_string(c["stronghold_skirmish"]["frags"].get<int>() - skirmish[tank_id].frags )+ ","
                                                + to_string(c["stronghold_skirmish"]["dropped_capture_points"].get<int>() - skirmish[tank_id].dcp )+ "),";  
                
                /** A update data v databaze v pvs_skirmish */
                *update_pvs_skirmish += "("           +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["stronghold_skirmish"]["battles"].get<int>())+ ","
                                                + to_string(c["stronghold_skirmish"]["wins"].get<int>())+ ","
                                                + to_string(c["stronghold_skirmish"]["spotted"].get<int>())+ ","
                                                + to_string(c["stronghold_skirmish"]["damage_dealt"].get<int>())+ ","
                                                + to_string(c["stronghold_skirmish"]["frags"].get<int>())+ ","
                                                + to_string(c["stronghold_skirmish"]["dropped_capture_points"].get<int>())+ "),";

                insert_skirmish_h_count ++; /** update pvs_skirmish a insertov do pvs_skirmish_historie musi by rovnako, tak ich nemusim pocitat zvlast */
            }      
          }
          else // ak nenajde tanky tak ich da na pridanie
          {
            *insert_pvs_skirmish += "("+account_id+"," 
                                    + to_string(c["tank_id"].get<int>()) +","
                                    + to_string(c["stronghold_skirmish"]["battles"].get<int>())+ ","
                                    + to_string(c["stronghold_skirmish"]["wins"].get<int>())+ ","
                                    + to_string(c["stronghold_skirmish"]["spotted"].get<int>())+ ","
                                    + to_string(c["stronghold_skirmish"]["damage_dealt"].get<int>())+ ","
                                    + to_string(c["stronghold_skirmish"]["frags"].get<int>())+ ","
                                    + to_string(c["stronghold_skirmish"]["dropped_capture_points"].get<int>())+ "),";         
            insert_skirmish_count ++;
          }
          
    }
    js.clear(); j.clear(); c.clear(); json_data.clear();  
}

void UrobPvsdefense(string json_data,string *insert_pvs_defense, string *insert_pvs_defense_history, string *update_pvs_defense)
{
       
    using json = nlohmann::json;
    json js,j,c;
    string account_id;

    try {        
        js = json::parse(json_data);        
    }
    catch(json::parse_error& e) {
        cout << "Parser UrobPvsAll: " << e.what() << endl;
        fails ++ ;
    }

    js      = js["data"];
    for (auto& x : json::iterator_wrapper(js))
    {
        account_id = x.key();
        j = x.value();
    }   
    
    for (auto& y : json::iterator_wrapper(j))
    {
          c = y.value();  
          int tank_id = c["tank_id"].get<int>();
      
          
          if(defense.count(tank_id) > 0 ) // ak najde taky tank_id v kontajnery
          {
            
            
            if(defense[tank_id].battles != c["stronghold_defense"]["battles"].get<int>() ) // Ak su rozdielne bitky tak to zaznamenaj
            {
                /** Vloz data do databazy do pvs_defense_history */
                *insert_pvs_defense_history +=  "("  +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["stronghold_defense"]["battles"].get<int>() - defense[tank_id].battles)+ ","
                                                + to_string(c["stronghold_defense"]["wins"].get<int>() - defense[tank_id].wins  )+ ","
                                                + "current_date - interval '1 days',"
                                                + to_string(c["stronghold_defense"]["spotted"].get<int>()  - defense[tank_id].spot )+ ","
                                                + to_string(c["stronghold_defense"]["damage_dealt"].get<int>() - defense[tank_id].dmg )+ ","
                                                + to_string(c["stronghold_defense"]["frags"].get<int>() - defense[tank_id].frags )+ ","
                                                + to_string(c["stronghold_defense"]["dropped_capture_points"].get<int>() - defense[tank_id].dcp )+ "),";  
                
                /** A update data v databaze v pvs_defense */
                *update_pvs_defense += "("           +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["stronghold_defense"]["battles"].get<int>())+ ","
                                                + to_string(c["stronghold_defense"]["wins"].get<int>())+ ","
                                                + to_string(c["stronghold_defense"]["spotted"].get<int>())+ ","
                                                + to_string(c["stronghold_defense"]["damage_dealt"].get<int>())+ ","
                                                + to_string(c["stronghold_defense"]["frags"].get<int>())+ ","
                                                + to_string(c["stronghold_defense"]["dropped_capture_points"].get<int>())+ "),";

                insert_defense_h_count ++; /** update pvs_defense a insertov do pvs_defense_historie musi by rovnako, tak ich nemusim pocitat zvlast */
            }      
          }
          else // ak nenajde tanky tak ich da na pridanie
          {
            *insert_pvs_defense += "("+account_id+"," 
                                    + to_string(c["tank_id"].get<int>()) +","
                                    + to_string(c["stronghold_defense"]["battles"].get<int>())+ ","
                                    + to_string(c["stronghold_defense"]["wins"].get<int>())+ ","
                                    + to_string(c["stronghold_defense"]["spotted"].get<int>())+ ","
                                    + to_string(c["stronghold_defense"]["damage_dealt"].get<int>())+ ","
                                    + to_string(c["stronghold_defense"]["frags"].get<int>())+ ","
                                    + to_string(c["stronghold_defense"]["dropped_capture_points"].get<int>())+ "),";         
            insert_defense_count ++;
          }
          
    }
   
    js.clear(); j.clear(); c.clear(); json_data.clear();
    
}

void UrobPvsMap(string json_data,string *insert_pvs_globalmap, string *insert_pvs_globalmap_history, string *update_pvs_globalmap)
{
       
    using json = nlohmann::json;
    
    json js,j,c;
    string account_id;
    try {        
        js = json::parse(json_data);        
    }
    catch(json::parse_error& e) {
        cout << "Parser UrobPvsMap: " << e.what() << endl;
        fails ++ ;
    }

    js      = js["data"];
    
    for (auto& x : json::iterator_wrapper(js))
    {
        account_id = x.key();
        j = x.value();
    }   
      
    for (auto& y : json::iterator_wrapper(j))
    {
          c = y.value();  
          int tank_id = c["tank_id"].get<int>();
          
          if(globalmap.count(tank_id) > 0 ) // ak najde taky tank_id v kontajnery
          {
            
            if(globalmap[tank_id].battles != c["globalmap"]["battles"].get<int>() ) // Ak su rozdielne bitky tak to zaznamenaj
            {
                /** Vloz data do databazy do pvs_globalmap_history */
                *insert_pvs_globalmap_history +=  "("  +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["globalmap"]["battles"].get<int>() - globalmap[tank_id].battles)+ ","
                                                + to_string(c["globalmap"]["wins"].get<int>() - globalmap[tank_id].wins  )+ ","
                                                + "current_date - interval '1 days',"
                                                + to_string(c["globalmap"]["spotted"].get<int>()  - globalmap[tank_id].spot )+ ","
                                                + to_string(c["globalmap"]["damage_dealt"].get<int>() - globalmap[tank_id].dmg )+ ","
                                                + to_string(c["globalmap"]["frags"].get<int>() - globalmap[tank_id].frags )+ ","
                                                + to_string(c["globalmap"]["dropped_capture_points"].get<int>() - globalmap[tank_id].dcp )+ "),";  
                
                /** A update data v databaze v pvs_globalmap */
                *update_pvs_globalmap += "("           +account_id+"," 
                                                + to_string(c["tank_id"].get<int>()) +","
                                                + to_string(c["globalmap"]["battles"].get<int>())+ ","
                                                + to_string(c["globalmap"]["wins"].get<int>())+ ","
                                                + to_string(c["globalmap"]["spotted"].get<int>())+ ","
                                                + to_string(c["globalmap"]["damage_dealt"].get<int>())+ ","
                                                + to_string(c["globalmap"]["frags"].get<int>())+ ","
                                                + to_string(c["globalmap"]["dropped_capture_points"].get<int>())+ "),";

                insert_map_h_count ++; /** update pvs_globalmap a insertov do pvs_globalmap_historie musi by rovnako, tak ich nemusim pocitat zvlast */
            }      
          }
          else // ak nenajde tanky tak ich da na pridanie
          {
            *insert_pvs_globalmap += "("+account_id+"," 
                                    + to_string(c["tank_id"].get<int>()) +","
                                    + to_string(c["globalmap"]["battles"].get<int>())+ ","
                                    + to_string(c["globalmap"]["wins"].get<int>())+ ","
                                    + to_string(c["globalmap"]["spotted"].get<int>())+ ","
                                    + to_string(c["globalmap"]["damage_dealt"].get<int>())+ ","
                                    + to_string(c["globalmap"]["frags"].get<int>())+ ","
                                    + to_string(c["globalmap"]["dropped_capture_points"].get<int>())+ "),";         
            insert_map_count ++;
          }
          
    }
    js.clear(); j.clear(); c.clear(); json_data.clear(); 
}

int Spracuj()
{   
    int i;
    string json_data;

    for(i = 1; i < 16; i ++)
    {
        json_data = CitajSubor(i);
        GetDatabaseData(json_data);
        json_data.clear();

    }

    return 0;
}   


int main()
{
    time_t start, stop;
    time(&start);
    
    cout << "*********************************"<< endl;
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;
    
    /** Vytvor jedno spojenie do databazy */
    VytvorSpojenie();

    /** Vacuum a analyz tabulike */
    VacuumAnalyze();

    /** Ziskanie relevantnych account_id */
    GetAccountId();
    cout << "Ma sa spracovat\t" << player_counter << " hracov" << endl;
    
    while(account_id.size() > 0)
    {
        /** Ziskaj json */
        GetJson();
        
        /** Spracuj json a uloz do databazy*/
        Spracuj();        
    }
    
    time(&stop);

    VacuumAnalyze();

    cout << "Celkovy pocer dotazov:\t"    << counter[0]+counter[1]+counter[2]+counter[3]+counter[4]+counter[5]+counter[6]+counter[7] << endl;
    cout << "pvs_all dotazov:\t" << counter[0] << endl; 
    cout << "pvs_all_h dotazov:\t" <<  counter[1] << endl;
    cout << "pvs_skirmish:\t\t" << counter[2]  << endl;
    cout << "pvs_skirmish_h:\t\t" << counter[3] << endl;
    cout << "pvs_defense:\t\t" << counter[4] << endl;
    cout << "pvs_defense_h:\t\t" << counter[5] << endl;
    cout << "pvs_globalmap:\t\t" << counter[6] << endl;
    cout << "pvs_globalmap_h:\t" << counter[7]  << endl;

    cout << "****************************************" << endl;
    cout << "Pocet chybnych json:\t" << fails << endl;
    cout << "Zostalo nespracovanych hracov:\t" << player_counter << endl;

    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;
    return 0;
}
