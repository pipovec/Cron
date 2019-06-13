#include <iostream>
#include <thread>
#include <libpq-fe.h>
#include <sys/time.h>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
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
            this->VacuumAnalyze();
            delete pgsql;
        }

        void VacuumAnalyze()
        {
            string query = "VACUUM FULL ANALYZE pvs_all";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_defense";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_skirmish";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_globalmap";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_all_history";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_defense_history";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_skirmish_history";
                PQsendQuery(this->conn,query.c_str());
            query = "VACUUM FULL ANALYZE pvs_globalmap_history";
                PQsendQuery(this->conn,query.c_str());
        }
        
        
        // Ziskam pocet hracov v databaze
        int GetPocet()
        {
            PGresult *result;
            int i; string res;
            const char *query = "SELECT account_id FROM players_stat_all_history WHERE date = current_date - interval '1 days ' AND battles > 0";

            //string query = "SELECT count(*) FROM cz_players";
            result = PQexec(this->conn, query);
            res = PQgetvalue(result,0,0); 
            PQclear(result);
            i   = stoi(res); 
            return i;
        }

        // Ziskavanie account_id do containera
        void GetPlayers(container *aids,int *ntuples)
        {
            PGresult *result;

            const char *query = "SELECT account_id FROM players_stat_all_history WHERE date = current_date - interval '1 days ' AND battles > 0";
            //string query    = "SELECT account_id FROM cz_players";
            result          = PQexec(this->conn, query);
                 if (PQresultStatus(result) != PGRES_TUPLES_OK)
                        {cout << "GetPlayers: " <<  PQresultErrorMessage(result) << endl;}

            int riadkov     = PQntuples(result);
            *ntuples        = riadkov; // aby som vedel kolko hracov budem spracovavat
            int i;
            
            for(i = 0; i < riadkov; i++)
            {
                (*aids)[i]  = stoi (PQgetvalue(result,i,0));
                
            }
            PQclear(result);
        }

        ~Players()
        {
             PQfinish(this->conn);
        }
        

};

// Poslanie dat account_id na server a ziskanie JSON
void SendPost(int account_id, string *json)
{
    const char text[] = "&fields=stronghold_skirmish.battles,stronghold_skirmish.wins,stronghold_skirmish.damage_dealt,stronghold_skirmish.spotted,stronghold_skirmish.frags,stronghold_skirmish.dropped_capture_points,"
    "globalmap.battles,globalmap.wins,globalmap.damage_dealt,globalmap.spotted,globalmap.frags,globalmap.dropped_capture_points,"
    "stronghold_defense.battles,stronghold_defense.wins,stronghold_defense.damage_dealt,stronghold_defense.spotted,stronghold_defense.frags,stronghold_defense.dropped_capture_points,"
    "all.battles,all.wins,all.damage_dealt,all.spotted,all.frags,all.dropped_capture_points,tank_id";

    string field(text);

    const string method   = "/tanks/stats/";

    string post_data = field  + "&account_id="+ to_string(account_id);
    SendCurl send;
    *json = send.SendWOT(method, post_data);

}

string Datum()
{
    // Datum
    string datum; // vcerajsi
    string rok,mesiac,den;
    time_t dnes = time(0);
    struct tm * now = localtime(&dnes);
    rok         = to_string((now->tm_year + 1900));
    mesiac      = to_string((now->tm_mon+1));
    den         = to_string((now->tm_mday-1));

    datum = rok + "-" + mesiac + "-" + den;

    return datum;
}

// Spracovanie JSON
void Json(int account_id, string json)
{
    int velkost_json = json.length();
    int zaciatok = 0; int koniec = 0; int koniec2 = 0;
    
    PGconn *conn;
    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();
    delete pgsql;

    void SpracujTank(int account_id, string data_tank, PGconn *conn);
    // Ci ma vobec nejake dataHraca
        zaciatok = json.find(to_string(account_id),0);
        koniec   = json.find("}",zaciatok);
        
    if((koniec - zaciatok) > 20)
    {

        do
        {
            zaciatok = json.find("stronghold_defense", koniec2);
            koniec   = json.find("tank_id",zaciatok);
            koniec2  = json.find("}", koniec);

            string data_tank = json.substr(zaciatok, (koniec2+1) - zaciatok);
            SpracujTank(account_id,data_tank,conn);
            
            data_tank.clear();
        }
        while(koniec2+5 < velkost_json);
    }        
    PQfinish(conn);
}

void SpracujTank(int account_id, string data_tank, PGconn *conn) 
{
    
    void VehicleStat(string data, int *Stat, string table);
    //void ExecPreparedStatment(string table, PGconn *conn, int account_id,int tank_id, int *riadkov,int *databaseStat);
    void Select(string table, PGconn *conn, int account_id,int tank_id, int *riadkov,int *databaseStat);
    void OnlyInsert(PGconn *conn,string table,int *pStat,int account_id);
    void Upsert(PGconn *conn,string table,int *pStat,int *pdStat, int account_id);

    int defense[7];int skirmish[7];int globalmap[7];int all[7];
    int *p_defense, *p_skirmish,*p_globalmap,*p_all;
    int databaseStat[7];int *p_databaseStat; p_databaseStat = databaseStat;
    p_defense = defense; p_skirmish = skirmish;p_globalmap = globalmap; p_all = all;

    VehicleStat(data_tank,p_defense,"stronghold_defense");
    VehicleStat(data_tank,p_skirmish,"stronghold_skirmish");
    VehicleStat(data_tank,p_globalmap,"globalmap");
    VehicleStat(data_tank,p_all,"all");

    int riadkov; int *p_riadkov; p_riadkov = &riadkov;

    databaseStat[0]=databaseStat[1]=databaseStat[2]=databaseStat[3]=databaseStat[4]=databaseStat[5]=databaseStat[6]=0;
    Select("pvs_defense",conn,account_id,defense[6],p_riadkov,p_databaseStat);
    if(riadkov == 0)
    {OnlyInsert(conn,"pvs_defense",p_defense,account_id);}
    else
    {Upsert(conn,"pvs_defense",p_defense,p_databaseStat,account_id);}

    databaseStat[0]=databaseStat[1]=databaseStat[2]=databaseStat[3]=databaseStat[4]=databaseStat[5]=databaseStat[6]=0;
    Select("pvs_skirmish",conn,account_id,skirmish[6],p_riadkov,p_databaseStat);
    if(riadkov == 0)
    {OnlyInsert(conn,"pvs_skirmish",p_skirmish,account_id);}
    else
    {Upsert(conn,"pvs_skirmish",p_skirmish,p_databaseStat,account_id);}

    databaseStat[0]=databaseStat[1]=databaseStat[2]=databaseStat[3]=databaseStat[4]=databaseStat[5]=databaseStat[6]=0;
    Select("pvs_globalmap",conn,account_id,globalmap[6],p_riadkov,p_databaseStat);
    if(riadkov == 0)
    {OnlyInsert(conn,"pvs_globalmap",p_globalmap,account_id);}
    else
    {Upsert(conn,"pvs_globalmap",p_globalmap,p_databaseStat,account_id);}

    databaseStat[0]=databaseStat[1]=databaseStat[2]=databaseStat[3]=databaseStat[4]=databaseStat[5]=databaseStat[6]=0;
    Select("pvs_all",conn,account_id,all[6],p_riadkov,p_databaseStat);
    if(riadkov == 0)
    {OnlyInsert(conn,"pvs_all",p_all,account_id);}
    else
    {Upsert(conn,"pvs_all",p_all,p_databaseStat,account_id);}

}



// Vyberie len udaje ktore su relevantne pre vypocet WN8
void VehicleStat(string data, int *Stat, string table)
{
    
    
    string dmg,spot,frag,def,battles,wins,tank_id;
    string all; // Budem hladat iba tieto statt
    int zac, kon;
    
    zac = data.find(table);
    kon = data.find("}",zac);
    all = data.substr(zac, (kon+1) - zac);
   
    // damage_dealt
    zac = all.find("\"spotted\"");
    kon = all.find(",", zac);
    spot = all.substr(zac + 10, kon - (zac + 10));
    
    // spot
    zac = all.find("\"wins\"");
    kon = all.find(",", zac);
    wins = all.substr(zac + 7, kon - (zac + 7));
    
    // frag
    zac = all.find("\"battles\"");
    kon = all.find(",", zac);
    battles = all.substr(zac + 10, kon - (zac + 10));
    
    //dropped_capture_points
    zac = all.find("\"damage_dealt\"");
    kon = all.find(",", zac);
    dmg = all.substr(zac + 15, kon - (zac + 15));
    
    // battles
    zac = all.find("\"frags\"");
    kon = all.find(",", zac);
    frag = all.substr(zac + 8, kon - (zac + 8));
    
    // dpc
    zac = all.find("\"dropped_capture_points\"");
    kon = all.find("}", zac);
    def = all.substr(zac + 25, kon - (zac + 25));
    

    // tank_id
    zac = data.find("\"tank_id\"");
    kon = data.find("}", zac);
    tank_id = data.substr(zac + 10, kon - (zac + 10));
    
    Stat[0]     = stoi(dmg);
    Stat[1]     = stoi(spot);
    Stat[2]     = stoi(frag);
    Stat[3]     = stoi(def);
    Stat[4]     = stoi(battles);
    Stat[5]     = stoi(wins);
    Stat[6]     = stoi(tank_id);
    
    all.clear();table.clear();data.clear();
}

void PreparedStatment(string table, PGconn *conn)
{
    PGresult *result;
    const char *ttable = table.c_str();
    string query  = "SELECT damage_dealt,spotted,frags,dropped_capture_points,battles,wins FROM " + table + " WHERE account_id = $1 and tank_id = $2";
    
    result = PQprepare(conn,ttable,query.c_str(),2,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment je chybny: %s", PQerrorMessage(conn));
        PQclear(result);
    }
    PQclear(result);
}

// Ci uz ma v historii za vcerajsok nieco
void PreparedStatment2(string table, PGconn *conn)
{
    PGresult *result;
    table = table+"_history";cout << table << endl;
    const char *ttable = table.c_str();
    string Datum();
    string datum = Datum();
    string query  = "SELECT account_id FROM " + table + " WHERE account_id=$1 and tank_id=$2 and date='" +datum+"'";
    
    result = PQprepare(conn,ttable,query.c_str(),2,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment2 je chybny: %s", PQresultErrorMessage(result));
        PQclear(result);
    }
    PQclear(result);
}

void Select(string table, PGconn *conn, int account_id,int tank_id, int *riadkov,int *databaseStat)
{
    PGresult *result;

    string query  = "SELECT damage_dealt,spotted,frags,dropped_capture_points,battles,wins,tank_id FROM " + table + " WHERE account_id = "+to_string(account_id)+" and tank_id ="+to_string(tank_id);
    *riadkov = 0;
    
    result  = PQexec(conn,query.c_str());
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "select from " + table << " je chybny: " << PQresultErrorMessage(result) << endl;}

    *riadkov  = PQntuples(result); // Signal ci uz mam nejake data v databaze 1-ano 0-nie
    if(*riadkov > 0)
    {
        *(databaseStat+0) = stoi(PQgetvalue(result,0,0)); //dmg
        *(databaseStat+1) = stoi(PQgetvalue(result,0,1)); // spotted
        *(databaseStat+2) = stoi(PQgetvalue(result,0,2)); // frags
        *(databaseStat+3) = stoi(PQgetvalue(result,0,3)); // def
        *(databaseStat+4) = stoi(PQgetvalue(result,0,4)); // battles
        *(databaseStat+5) = stoi(PQgetvalue(result,0,5)); // wins
        *(databaseStat+6) = stoi(PQgetvalue(result,0,6)); // tank_id
         
    }
    else
    {
        *(databaseStat+0) = *(databaseStat+1) = *(databaseStat+2) = *(databaseStat+3) = *(databaseStat+4) = *(databaseStat+5) = *(databaseStat+6) = 0;  
    } 

    PQclear(result);

}

void ExecPreparedStatment(string table, PGconn *conn, int account_id,int tank_id, int *riadkov,int *databaseStat)
{
    PGresult *result;
    const char *ttable = table.c_str();
    const char *paramValues[1];
    string a_id = to_string(account_id);
    string t_id = to_string(databaseStat[6]);
    paramValues[0] = a_id.c_str();
    paramValues[1] = t_id.c_str();
    *riadkov = 0;
    
    result  = PQexecPrepared(conn,ttable,2,paramValues,NULL,NULL,0);
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {fprintf(stderr, "Select statment je chybny: %s", PQresultErrorMessage(result));}

    *riadkov  = PQntuples(result); // Signal ci uz mam nejake data v databaze 1-ano 0-nie
    if(*riadkov > 0)
    {
        *(databaseStat+0) = stoi(PQgetvalue(result,0,0));
        *(databaseStat+1) = stoi(PQgetvalue(result,0,1));
        *(databaseStat+2) = stoi(PQgetvalue(result,0,2));
        *(databaseStat+3) = stoi(PQgetvalue(result,0,3));
        *(databaseStat+4) = stoi(PQgetvalue(result,0,4));
        *(databaseStat+5) = stoi(PQgetvalue(result,0,5));
        *(databaseStat+6) = stoi(PQgetvalue(result,0,6));
    }
    else
    {
        *(databaseStat+0) = *(databaseStat+1) = *(databaseStat+2) = *(databaseStat+3) = *(databaseStat+4) = *(databaseStat+5) = *(databaseStat+6) = 0;  
    } 

    PQclear(result);

}

void OnlyInsert(PGconn *conn,string table,int *pStat,int account_id)
{
    PGresult *result;
    
    string query = "INSERT INTO " + table + "(damage_dealt,spotted,frags,dropped_capture_points,battles,wins,tank_id,account_id)" +
                  + "VALUES (" +to_string( *(pStat+0)) + ","+to_string(*(pStat+1))+","+to_string(*(pStat+2))+","+to_string(*(pStat+3))+","+to_string(*(pStat+4))+","
                    +to_string(*(pStat+5))+","+to_string(*(pStat+6))+","+to_string(account_id)+")";
    
    if(pStat[4] > 0)
    {
        result = PQexec(conn, query.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba OnlyInsert do " << table+": " <<  PQresultErrorMessage(result) << endl;}
        PQclear(result);
    }

    query.clear();
    
}

void Upsert(PGconn *conn,string table,int *pStat,int *pdStat, int account_id)
{
    string Datum();
    string datum = Datum();
    int ntuples;

    int cBattles = (pStat[4] - pdStat[4]); // Skontrolujem ci nieco odohral
    PGresult *result;
    if(cBattles > 0 )
    {
    
        string select = "SELECT tank_id FROM "+table+"_history WHERE account_id="+to_string(account_id)+" and tank_id="+to_string(pStat[6])+" and date = '"+datum+"'";

        result = PQexec(conn,select.c_str());
         if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "select v upsert from " + table << " je chybny: " << PQresultErrorMessage(result) << endl;}

        ntuples = PQntuples(result);
        
        if(ntuples == 0)
        {
            string insert = "INSERT INTO "+ table +"_history (damage_dealt,spotted,frags,dropped_capture_points,battles,wins,tank_id,date,account_id) VALUES ("+to_string(pStat[0] - pdStat[0])+","+to_string(pStat[1] - pdStat[1])+
                        ","+to_string(pStat[2] - pdStat[2])+","+to_string(pStat[3] - pdStat[3])+","+to_string(pStat[4] - pdStat[4])+","+to_string(pStat[5] - pdStat[5])+
                        ","+to_string(pStat[6])+",'"+datum+"' ,"+to_string(account_id)+")";
                
            result = PQexec(conn, insert.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba insertu do " << table+"_history: " <<  PQresultErrorMessage(result) << endl;}

            insert.clear();PQclear(result);
        }

            string update = "UPDATE "+table+" SET damage_dealt = "+to_string(pStat[0])+",spotted = "+to_string(pStat[1])+",frags = "+to_string(pStat[2])+",dropped_capture_points = "+to_string(pStat[3])+
                            ",battles = "+to_string(pStat[4])+",wins = "+to_string(pStat[5])+" WHERE account_id = "+to_string(account_id) +" and tank_id = "+to_string(pStat[6]); 

            result = PQexec(conn,update.c_str());
                if (PQresultStatus(result) != PGRES_COMMAND_OK)
                
                {cout << "Chyba update do "  << update <<  PQresultErrorMessage(result) << endl;}                 
            
            
            update.clear();PQclear(result);
               
    }
}


int main()
{
    
    /* Informacia o case zacatia programu */
    time_t start, stop;
    time(&start);
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;
   
    Players players;
    // Najprv zistim kolko hracov budem spracovavat
    int ntuples; int *p_ntuples; p_ntuples = &ntuples; //Kolko hracov sa mi vratilo z databazy
    int riadkov = players.GetPocet();

    /*  Sem si deklarujem potrebne premenne aby sa v cykle nehromazdili */
    container aids; container *p_aids; p_aids = &aids; // unordered_map na account_id
    players.GetPlayers(p_aids, p_ntuples); // ziskam do unordered_map int account_id
   
    void SendPost(int account_id, string *json);
    void Json(int account_id, string json);

    

    string j1,j2,j3,j4,j5,j6,j7,j8,j9,j10;
    string *p_j1,*p_j2,*p_j3,*p_j4,*p_j5,*p_j6,*p_j7,*p_j8,*p_j9,*p_j10;
    p_j1 = &j1;p_j2 = &j2;p_j3 = &j3;p_j4 = &j4;p_j5 = &j5;
    p_j6 = &j6;p_j7 = &j7;p_j8 = &j8;p_j9 = &j9;p_j10 = &j10;

   // double S,J,C;
    timestamp_t t0 = get_timestamp();
    cout << "Ma sa spracovat "<<riadkov<< " hracov" << endl;
    
    for(int i = 0; i < riadkov; i = i+10) // Hracov budem spracovavat po 10
    {
        
        if((riadkov - i) >= 10)
        {
            //timestamp_t S0 = get_timestamp();
            thread T1(SendPost,aids[i],p_j1) ;
            thread T2(SendPost,aids[i+1],p_j2) ; 
            thread T3(SendPost,aids[i+2],p_j3) ; 
            thread T4(SendPost,aids[i+3],p_j4) ;  
            thread T5(SendPost,aids[i+4],p_j5) ; 
            thread T6(SendPost,aids[i+5],p_j6) ; 
            thread T7(SendPost,aids[i+6],p_j7) ; 
            thread T8(SendPost,aids[i+7],p_j8) ; 
            thread T9(SendPost,aids[i+8],p_j9) ; 
            thread T10(SendPost,aids[i+9],p_j10) ; 
            
            T1.join();T2.join();T3.join();T4.join();T5.join();T6.join();T7.join();T8.join();T9.join();T10.join();
            //timestamp_t S1 = get_timestamp();

            //timestamp_t json0 = get_timestamp();
            /* Spracovanie JSON a poslanie dat do databazy */
           thread J1(Json,aids[i],j1);
           thread J2(Json,aids[i+1],j2);
           thread J3(Json,aids[i+2],j3);
           thread J4(Json,aids[i+3],j4);
           thread J5(Json,aids[i+4],j5);
           thread J6(Json,aids[i+5],j6);
           thread J7(Json,aids[i+6],j7);
           thread J8(Json,aids[i+7],j8);
           thread J9(Json,aids[i+8],j9);
           thread J10(Json,aids[i+9],j10);
           J1.join();J2.join();J3.join();J4.join();J5.join();J6.join();J7.join();J8.join();J9.join();J10.join();
           
           //timestamp_t json1 = get_timestamp();

          /*
           S = (S1 - S0) / 2000000.0L;
           J = (json1 - json0) / 2000000.0L;
           C = (json1 - t0) / 2000000.0L;

           cout << "Spracovanych:\t\t" << i << endl;
           cout << "Cas zatial:\t\t" << C << endl;
           cout << "Cas SendPost:\t\t" << S << endl;
           cout << "Cas JSON:\t\t" << J << endl << "=====================" << endl << endl;

           */
           
        }
        j1.clear();j2.clear();j3.clear();j4.clear();j5.clear();j6.clear();j7.clear();j8.clear();j9.clear();j10.clear();
    }

    aids.erase(aids.begin(), aids.end());
    players.VacuumAnalyze();

    timestamp_t t2 = get_timestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;
    return 0;
}