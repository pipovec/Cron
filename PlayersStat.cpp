#include <iostream>
#include <thread>
#include <libpq-fe.h>
#include <sys/time.h>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"
#include <unordered_map>

/* Meranie casu */
typedef unsigned long long timestamp_t;
typedef unordered_map<int,int> container;


static timestamp_t get_timestamp ()
{
      struct timeval now;
      gettimeofday (&now, NULL);
      return  now.tv_usec + (timestamp_t)now.tv_sec * 2000000;
}

using namespace std;

class Players 
{
    public:
        PGconn *conn;
        Players()
        {
            Pgsql *pgsql    = new Pgsql;
            this->conn      = pgsql->Get();            
                this->CheckConnection(this->conn);
            
        }

        // Ziskam pocet hracov v databaze
        int GetPocet()
        {
            PGresult *result;
            int i; string res;

            //string query = "SELECT count(*) FROM cz_players"; 
            string query = "select count(distinct(account_id)) from members_role where clan_id IN (select clan_id from tmp_clans_cs)" ;
            result = PQexec(this->conn, query.c_str());
                this->CheckResult(result);            

            res = PQgetvalue(result,0,0); 
            PQclear(result);
            i   = stoi(res); 
            return i;
        }

        // Ziskavanie account_id po 500ks 
        void GetPlayers(int offset, container *account_ids,int *ntuples)
        {
            PGresult *result;
            //string query    = "SELECT account_id FROM cz_players OFFSET " + to_string(offset) + " LIMIT 500";
            string query    = "select distinct(account_id) from members_role where clan_id IN (select clan_id from tmp_clans_cs) OFFSET " + to_string(offset) + " LIMIT 500";
            result          = PQexec(this->conn, query.c_str());
                this->CheckResult(result); 
            int riadkov     = PQntuples(result);
            *ntuples        = riadkov;
            int i;
            
            for(i = 0; i < riadkov; i++)
            {
                (*account_ids)[i]  = stoi (PQgetvalue(result,i,0));
                
            }
            PQclear(result);
        }

        ~Players()
        {
             PQfinish(this->conn);
        }

    private:
        void CheckConnection(PGconn *connection) {
            if (PQstatus(connection) != CONNECTION_OK)            {
                fprintf(stderr, "Connection to database failed: %s",
                        PQerrorMessage(connection));                
            }
        }
        void CheckResult(PGresult *result) {
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {
                fprintf(stderr, "SET failed: %s", PQerrorMessage(this->conn));
                PQclear(result);                        
            }
        }

};

// Rozsekam int account_id na jeden string so 100 account_id oddeleny ciarkami pre posielanie dotazov
void GetAccountId(unsigned stovka, container *account_ids, string *ids)
{
    
    for(unsigned i = stovka-100; i < stovka; i++)
    {
        if(account_ids->bucket_count() > i)
            {*ids += to_string((*account_ids)[i]) + ",";}
    }

    ids->erase(ids->end()-1); // Vymaze poslednu ciarku
}

// Poslanie dat account_id na server a ziskanie JSON
void SendPost(string *aids, string *json)
{
    string field = "&fields=-statistics.historical,-statistics.clan,-statistics.regular_team,-statistics.fallout,-statistics.team,-statistics.company,-clan_id,-global_rating,-client_language,-last_battle_time,-created_at,-updated_at";
    string extra = "&extra=statistics.globalmap_absolute,statistics.globalmap_champion";
    const string method   = "/account/info/";

    if(aids->size() > 0)
    {
        string post_data = field + extra + "&account_id="+*aids;
        SendCurl send;
        *json = send.SendWOT(method, post_data);
    }
}
// Spracovanie JSON
void Json(string *aids, string *json_data, int *p_ntuples, int stovka)
{
    if(*p_ntuples > (stovka-100))
    {
        Pgsql pgsql;
        PGconn *conn = pgsql.Get();
        if (PQstatus(conn) != CONNECTION_OK)            {
                fprintf(stderr, "Connection to database failed: %s",
                        PQerrorMessage(conn));                
            }
        
        string DataHraca(string *aids, string *json, string account_id);
        void PlayersStat(string data, int *pStatAll, string table);
        
        string data,account_id;
        int riadkov;
        int StatAll[7],StatGMc[7],StatGMa[7],StatSkir[7],StatDef[7];
        int dStatAll[7],dStatGMc[7],dStatGMa[7],dStatSkir[7],dStatDef[7];

        int *priadkov; priadkov = &riadkov;

        int *pStatAll; pStatAll = StatAll;int *pStatGMc; pStatGMc = StatGMc;int *pStatGMa; pStatGMa = StatGMa;
        
        int *pStatSkir; pStatSkir = StatSkir;int *pStatDef; pStatDef = StatDef;

        
        int *pdStatAll; pdStatAll = dStatAll;int *pdStatGMc; pdStatGMc = dStatGMc;int *pdStatGMa; pdStatGMa = dStatGMa;
        
        int *pdStatSkir; pdStatSkir = dStatSkir;int *pdStatDef; pdStatDef = dStatDef;

        // Pripravim si dotazy do tabuliek
        void PreparedStatment(string table, PGconn *conn);
        void ExecPreparedStatment(string table, PGconn *conn, string account_id,int *riadkov,int *pdStat);
        void OnlyInsert(PGconn *conn,string table,int *pStat,string account_id);
        void Upsert(PGconn *conn,string table,int *pStat,int *pdStat, string account_id);
        PreparedStatment("players_stat_all", conn); PreparedStatment("players_stat_defense", conn);PreparedStatment("players_stat_skirmish", conn);
        PreparedStatment("players_stat_gm_absolute", conn); PreparedStatment("players_stat_gm_champion", conn);
        
        using json = nlohmann::json;
        json js;

        try {
        js = json::parse(*json_data);
        }
        catch(json::parse_error& e)
        {
            cout << "Parser 0: " << e.what() << endl;
            
        }
        js = js["data"];

        json j;

        /** Ziskanie account_id a hodnot */
        for (auto& x : json::iterator_wrapper(js))
        {
            account_id = x.key();            

            j = x.value();            
            data = j.dump();            
             
            
            PlayersStat(data,pStatAll,"all");
            PlayersStat(data,pStatGMc,"globalmap_champion");
            PlayersStat(data,pStatGMa,"globalmap_absolute");            
            PlayersStat(data,pStatDef,"stronghold_defense");
            PlayersStat(data,pStatSkir,"stronghold_skirmish");
            
            //TStatAll.join();TStatGMc.join();TStatGMa.join();TStatDef.join();TStatSkir.join();
            

            

            // Zacne pracovat s databazou 
            ExecPreparedStatment("players_stat_all",conn,account_id,priadkov,pdStatAll);
            if(riadkov == 0)
            { OnlyInsert(conn,"players_stat_all",pStatAll,account_id);} // Vlozim data ktore som ziskal zo servera
            else
            { Upsert(conn,"players_stat_all",pStatAll,pdStatAll,account_id);} // Vlozi data nove data a urobi historiu
            
            // Stronghold
            ExecPreparedStatment("players_stat_defense",conn,account_id,priadkov,pdStatDef);
            if(riadkov == 0)
            { OnlyInsert(conn,"players_stat_defense",pStatDef,account_id);} 
            else
            { Upsert(conn,"players_stat_defense",pStatDef,pdStatDef,account_id);}
            
            // Sarvatky
            ExecPreparedStatment("players_stat_skirmish",conn,account_id,priadkov,pdStatSkir);
            if(riadkov == 0)
            { OnlyInsert(conn,"players_stat_skirmish",pStatSkir,account_id);} 
            else
            { Upsert(conn,"players_stat_skirmish",pStatSkir,pdStatSkir,account_id);}
            
            // Global map X
            ExecPreparedStatment("players_stat_gm_absolute",conn,account_id,priadkov,pdStatGMa);
            if(riadkov == 0)
            { OnlyInsert(conn,"players_stat_gm_absolute",pStatGMa,account_id);} 
            else
            { Upsert(conn,"players_stat_gm_absolute",pStatGMa,pdStatGMa,account_id);}
            
            // Global map VIII
            ExecPreparedStatment("players_stat_gm_champion",conn,account_id,priadkov,pdStatGMc);
            if(riadkov == 0)
            { OnlyInsert(conn,"players_stat_gm_champion",pStatGMc,account_id);} 
            else
            { Upsert(conn,"players_stat_gm_champion",pStatGMc,pdStatGMc,account_id);}
            

        } 
        

        
    PQfinish(conn);
    }
}



// Vyberie len udaje ktore su relevantne pre vypocet WN8 a avgXP per battle, relevantne pre mna
void PlayersStat(string data, int *Stat, string table)
{
    
    using json = nlohmann::json;
    json j;

    try {
        j = json::parse(data);
    }catch(json::parse_error& e) {
        cout << "Parser v PlayersStat: " << e.what() << endl;
    }    
    
    // damage_dealt    
    Stat[0] = j["statistics"][table]["damage_dealt"].get<int>();    

    // spot    
    Stat[1] = j["statistics"][table]["spotted"].get<int>();

    // frag    
    Stat[2] = j["statistics"][table]["frags"].get<int>();

    //dropped_capture_points    
    Stat[3] = j["statistics"][table]["dropped_capture_points"].get<int>();

    // battles    
    Stat[4] = j["statistics"][table]["battles"].get<int>();

    // wins    
    Stat[5] = j["statistics"][table]["wins"].get<int>();

    // xp    
    Stat[6] = j["statistics"][table]["battle_avg_xp"].get<int>();    

    j= NULL;
   
}

void PreparedStatment(string table, PGconn *conn)
{
    PGresult *result;
    const char *ttable = table.c_str();
    string query  = "SELECT damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp FROM " + table +" WHERE account_id = $1";
    
    result = PQprepare(conn,ttable,query.c_str(),1,NULL);
    
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment je chybny: %s", PQerrorMessage(conn));
        PQclear(result);
    }
    PQclear(result);
}

void ExecPreparedStatment(string table, PGconn *conn, string account_id, int *riadkov,int *pdStat)
{
    PGresult *result;
    const char *ttable = table.c_str();
    const char *paramValues[1];
    paramValues[0] = account_id.c_str();
    *riadkov = 0;
    

    result  = PQexecPrepared(conn,ttable,1,paramValues,NULL,NULL,0);
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {
                fprintf(stderr, "Select prepared statment je chybny: %s", PQresultErrorMessage(result));            
            }

    *riadkov  = PQntuples(result); // Signal ci uz mam nejake data v databaze 1-ano 0-nie
    
    
    if(*riadkov > 0)
    {
        *(pdStat+0) = stoi(PQgetvalue(result,0,0));
        *(pdStat+1) = stoi(PQgetvalue(result,0,1));
        *(pdStat+2) = stoi(PQgetvalue(result,0,2));
        *(pdStat+3) = stoi(PQgetvalue(result,0,3));
        *(pdStat+4) = stoi(PQgetvalue(result,0,4));
        *(pdStat+5) = stoi(PQgetvalue(result,0,5));
        *(pdStat+6) = stoi(PQgetvalue(result,0,6));
    }
    else
    {
        *(pdStat+0) = *(pdStat+1) = *(pdStat+2) = *(pdStat+3) = *(pdStat+4) = *(pdStat+5) = *(pdStat+6) = 0;  
    } 

    PQclear(result);

}

void OnlyInsert(PGconn *conn,string table,int *pStat,string account_id)
{
    PGresult *result;
    
    string query = "INSERT INTO " + table + "(damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,account_id)" +
                  + "VALUES (" +to_string( *(pStat+0)) + ","+to_string(*(pStat+1))+","+to_string(*(pStat+2))+","+to_string(*(pStat+3))+","+to_string(*(pStat+4))+","
                    +to_string(*(pStat+5))+","+to_string(*(pStat+6))+","+account_id+")";
    
    result = PQexec(conn, query.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba OnlyInsert do " << table+": " <<  PQresultErrorMessage(result) << endl;}

    query.clear();
    
}

void Upsert(PGconn *conn,string table,int *pStat,int *pdStat, string account_id)
{
    
    PGresult *result;
    
        if( (pStat[4] - pdStat[4]) > 0){			
		string insert = "INSERT INTO "+ table +"_history (damage_dealt,spotted,frags,dropped_capture_points,battles,wins,battle_avg_xp,date,account_id) VALUES ("+to_string(pStat[0] - pdStat[0])+","+to_string(pStat[1] - pdStat[1])+
                    ","+to_string(pStat[2] - pdStat[2])+","+to_string(pStat[3] - pdStat[3])+","+to_string(pStat[4] - pdStat[4])+","+to_string(pStat[5] - pdStat[5])+
                    ","+to_string(pStat[6] - pdStat[6])+", now() - interval '1 day',"+account_id+")";
             
        result = PQexec(conn, insert.c_str());
         if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba insertu do " << table+"_history: " <<  PQresultErrorMessage(result) << endl;}

        insert.clear();PQclear(result);
		}
			
        string update = "UPDATE "+table+" SET damage_dealt = "+to_string(pStat[0])+",spotted = "+to_string(pStat[1])+",frags = "+to_string(pStat[2])+",dropped_capture_points = "+to_string(pStat[3])+
                        ",battles = "+to_string(pStat[4])+",wins = "+to_string(pStat[5])+",battle_avg_xp = "+to_string(pStat[6])+" WHERE account_id = "+account_id; 

        result = PQexec(conn,update.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba update do "  << table <<  PQresultErrorMessage(result) << endl;}                 
        
        PQclear(result);
        update.clear();        
    
}


int main()
{   
    timestamp_t t0 = get_timestamp();
    /* Informacia o case zacatia programu */
    time_t start, stop;
    time(&start);
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;
   
    Players players;
    // Najprv zistim kolko hracov budem spracovavat    
    int riadkov = players.GetPocet();
    cout << "Pocet riadkov " << riadkov << endl;
    /*  Sem si deklarujem potrebne premenne aby sa v cykle nehromazdili */
    container a_ids; container *p_a_ids; p_a_ids = &a_ids; // unordered_map na account_id
    
    // string so 100 account_id
    string aids1;string *p_aids1; p_aids1 = &aids1;
    string aids2;string *p_aids2; p_aids2 = &aids2;
    string aids3;string *p_aids3; p_aids3 = &aids3;
    string aids4;string *p_aids4; p_aids4 = &aids4;
    string aids5;string *p_aids5; p_aids5 = &aids5;

    //Odpovede v tvare JSON
    string json1; string *p_json1; p_json1 = &json1;
    string json2; string *p_json2; p_json2 = &json2;
    string json3; string *p_json3; p_json3 = &json3;
    string json4; string *p_json4; p_json4 = &json4;
    string json5; string *p_json5; p_json5 = &json5;
    
    void GetAccountId(unsigned stovka, container *account_ids, string *ids);
    void SendPost(string *aids, string *json);
    void Json(string *aids, string *json, int *p_ntuples, int stovka);

    int ntuples; int *p_ntuples; p_ntuples = &ntuples; //Kolko hracov sa mi vratilo z databazy
    int spracovanych = 0;

    for(int i = 0; i < riadkov; i += 500) // Tu si nastavim po kolko hracov naraz budem spracovavat
    {
        players.GetPlayers(i, p_a_ids, p_ntuples); // ziskam do unordered_map int account_id    



        if(ntuples < 500) {cout << "pocet hracov na spracovanie: " << ntuples << endl;}
        // premenim na to na string so 100 account_id oddeleny ciarkami
        if(ntuples > 0)     GetAccountId(100,p_a_ids,p_aids1); 
        if(ntuples > 100)   GetAccountId(200,p_a_ids,p_aids2);
        if(ntuples > 200)   GetAccountId(300,p_a_ids,p_aids3);
        if(ntuples > 300)   GetAccountId(400,p_a_ids,p_aids4);
        if(ntuples > 400)   GetAccountId(500,p_a_ids,p_aids5);

        
        
        /* Posielanie account_ids na server a z ziskanie JSON */
        if(ntuples > 0)     SendPost(p_aids1,p_json1);
        if(ntuples > 100)   SendPost(p_aids2,p_json2);
        if(ntuples > 200)   SendPost(p_aids3,p_json3);
        if(ntuples > 300)   SendPost(p_aids4,p_json4);
        if(ntuples > 400)   SendPost(p_aids5,p_json5);

       
         
        /* Spracovanie JSON a poslanie dat do databazy */
        if(ntuples > 0)     Json(p_aids1,p_json1,p_ntuples,100);

        cout << "TU SOM "  << endl;
        if(ntuples > 100)   Json(p_aids2,p_json2,p_ntuples,200);
        if(ntuples > 200)   Json(p_aids3,p_json3,p_ntuples,300);
        if(ntuples > 300)   Json(p_aids4,p_json4,p_ntuples,400);
        if(ntuples > 400)   Json(p_aids5,p_json5,p_ntuples,500);
        
         cout << "TU " << i << endl;
        a_ids.erase(a_ids.begin(), a_ids.end());
        aids1.clear();aids2.clear();aids3.clear();aids4.clear();aids5.clear();
        json1.clear();json2.clear();json3.clear();json4.clear();json5.clear();
        
        spracovanych = spracovanych + ntuples;
        cout << "spracovanych: " << spracovanych << endl;

        ntuples = 0;
       
    }



    timestamp_t t2 = get_timestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;
    return 0;
}