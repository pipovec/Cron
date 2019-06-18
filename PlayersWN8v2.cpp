#include <iostream>
#include <string>
#include <libpq-fe.h>
#include <sys/time.h>
#include "./lib/Pgsql.h"
#include "./lib/GetPlayersStats.h"
#include "./lib/GetPlayersVehicles.h"
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <thread>

using namespace std;

// Odkial sa budu brat account_id
const char* master_query = "SELECT account_id FROM players_all";

// Kedy sa maju pocitat WN8
const int pamataj = 2500;

/* Meranie casu */
typedef unsigned long long timestamp_t;

/* Container na account_id */
typedef unordered_map<int,int> mymap;

// Container pre hracske id */
typedef queue<int> Account_ids;

// Cointainer pre wn8 hodnoty
typedef unordered_map<int,float> wn8value;

// Vcerajsin datum ako string
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

static timestamp_t get_timestamp ()
{
      struct timeval now;
      gettimeofday (&now, NULL);
      return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

void GetPlayers(Account_ids *a_ids)
{
    PGconn *conn;
    PGresult *result;
    
    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();

    result = PQexec(conn,master_query);

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            cout << "Chyba v GetPlayers: " <<  PQresultErrorMessage(result) << endl;
            fprintf(stderr, "SET failed: %s", PQerrorMessage(conn));
            PQclear(result);

        }

    int ntuples = PQntuples(result);

    int i,id;
    for(i = 0; i < ntuples; i++)
    {
        id  = stoi (PQgetvalue(result,i,0));
        a_ids->push(id);
    }


    PQclear(result);
    PQfinish(conn);    
}

//Ziskam 100 hracsky id
string GetPlayersIds(Account_ids *p_aids)
{
    string id;
    
    for(int i = 0; i < 100; i++)
    {
        if(!p_aids->empty())
        {
            id += to_string(p_aids->front());
            id += ',';
            p_aids->pop();
        }
        
    }

    // Vymaze poslednu ciarku
    id.erase(id.end()-1);

   return id;

}




class WN8Player
{
    /* Vypocita hodnotu WN8 pre hraca
    *   Potrebuje unordered_map PlayerData a TankData
    *
    *
    */


    public:
        WN8Player()
        {
            this->expTable();
        }

        struct etv_data
        {
            float frag;
            float dmg;
            float spot;
            float def;
            float win;
        };

        typedef unordered_map<int, etv_data> etv_table;


        float GetWN8(wn_data PlayerData, vehicle_battles TankData)
        {
            float wn8 = 0;
            wn8  = this->Calculate(PlayerData,TankData);
            return wn8;
        }

    private:
        etv_table etv; // unordered_map pre etv tabulku
        etv_data  data; // struktura ktora je v  unordered_map pod klucom tank_id

        
        float Calculate(wn_data PlayerData, vehicle_battles TankData)
        {
            double expDMG,expSpot,expFrag,expDef,expWin;
	        expDMG = expSpot = expFrag = expDef = expWin = 0.00;
            etv_data tmp;

            int tank_id,battles;

            for (auto& x: TankData)
            {
                tank_id = x.first;
                battles = x.second;
                
                tmp = this->etv[tank_id];

                expDMG      += tmp.dmg  * (double)battles;
                expSpot     += tmp.spot * (double)battles;
                expFrag     += tmp.frag * (double)battles;
                expDef      += tmp.def  * (double)battles;
                expWin      += 0.01 * tmp.win * (double)battles;

                //cout << tank_id << " - "<< battles << " - " << tmp.dmg << endl;

            }

            double rDMG,rSpot,rFrag,rDef,rWin;
	        rDMG = rSpot = rFrag = rDef = rWin = 0.00;

            rDMG    = PlayerData.dmg  / expDMG;
            rSpot   = PlayerData.spot  / expSpot;
            rFrag   = PlayerData.frags  / expFrag;
            rDef    = PlayerData.dcp  / expDef;
            rWin    = PlayerData.wins  / expWin;

            //cout << PlayerData.dmg << " / "<< expDMG << " rWIN " << rWin << endl;

            double rDMGc,rSpotc,rFragc,rDefc,rWinc;
	        rDMGc = rSpotc = rFragc = rDefc = rWinc = 0.00;

            rDMGc   = max(0.00,                    (rDMG - 0.22) / (1.00 - 0.22));
            rSpotc  = max(0.00, min(rDMGc + 0.1,   (rSpot - 0.38) / (1.00 - 0.38)));
            rFragc  = max(0.00, min(rDMGc + 0.2,   (rFrag - 0.12) / (1.00 - 0.12)));
            rDefc   = max(0.00, min(rDMGc + 0.1,   (rDef -  0.10) / (1.00 - 0.10)));
            rWinc   = max(0.00,                    (rWin - 0.71) / (1 - 0.71));

            float wn8 = 980 * rDMGc + 210 * rDMGc * rFragc + 155 * rFragc * rSpotc + 75 * rDefc * rFragc + 145 * min(1.8,rWinc);



            return wn8;

        }


        void expTable()
        {
            PGresult *result;
            PGconn *conn;
            Pgsql *pgsql    = new Pgsql;
            conn = pgsql->Get();
            //delete pgsql;

            const char query[] = "SELECT * FROM expected_tank_value";

            result = PQexec(conn,query);
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
                 {cout << "Chyba v select etv table: " <<  PQresultErrorMessage(result) << endl;}

            int ntuples = PQntuples(result);int tank_id;int i;
            for(i = 0; i < ntuples; i++)
            {
                tank_id    = stoi(PQgetvalue(result,i,0));

                data.frag  = stof(PQgetvalue(result,i,1));
                data.dmg   = stof(PQgetvalue(result,i,2));
                data.spot  = stof(PQgetvalue(result,i,3));
                data.def   = stof(PQgetvalue(result,i,4));
                data.win   = stof(PQgetvalue(result,i,5));

                this->etv[tank_id] = data;

            }

            PQclear(result);
            PQfinish(conn);

        }





};

void SpocitajWN8(PlayersStats *p_pstats, Data_vehicle *p_vehicles, wn8value *p_wn8)
{
    WN8Player *wn8 = new WN8Player;
    float wn_value;
    int id;

    for(auto it= p_pstats->begin(); it != p_pstats->end(); it++) 
    {
        //cout << it->first << endl;
        id = it->first;
        auto players = it->second;  //Data hraca
        auto tanks = p_vehicles->at( id );
        
        wn_value = wn8->GetWN8( players, tanks);
        (*p_wn8)[ id ] = wn_value;

        //cout << "account_id: " << id << " : "  << wn_value << endl;        

    }

    delete wn8;
    // Mazanie map
    p_pstats->clear();
    p_vehicles->clear();
        
}

void UlozHraca(wn8value *p_wn8)
{
    PGconn *conn;
    PGresult *result;
    
    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();
    

    string q_delete = "DELETE FROM wn8player WHERE account_id IN (";
    string q_insert = "INSERT INTO wn8player (account_id,wn8) VALUES ";
    string del = "";
    string ins = "";

    int account_id;
    float wn_value;

    for(auto it = p_wn8->begin(); it != p_wn8->end(); it++ )
    {
        account_id = it->first;
        wn_value = it->second;

        del += to_string(account_id)+",";
        ins += "("+to_string(account_id)+","+to_string(wn_value)+"),";
    }
    
    // Vymaze poslednu ciarku
    del.erase(del.end()-1);
    ins.erase(ins.end()-1);

    q_delete += del + ")";
    q_insert += ins ;
    cout << q_delete << endl;
    result = PQexec(conn, q_delete.c_str());
        // if (PQresultStatus(result) != PGRES_TUPLES_OK)
        //        {cout << "DELETE error: " <<  PQresultErrorMessage(result) << endl;}
    PQclear(result);

    cout << q_insert << endl;
    result = PQexec(conn, q_insert.c_str());
        // if (PQresultStatus(result) != PGRES_TUPLES_OK)
        //        {cout << "INSERT error: " <<  PQresultErrorMessage(result) << endl;}
    PQclear(result);

    p_wn8->clear();q_delete.clear();q_insert.clear();del.clear();ins.clear();
    PQfinish(conn);
}

int main()
{
    /* Informacia o case zacatia programu */
    time_t start, stop;
    timestamp_t t0 = get_timestamp();
    time(&start);
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;

    Account_ids aids; Account_ids *p_aids; p_aids = &aids;  
    
    PlayersStats pstats; PlayersStats *p_pstats; 
    p_pstats = &pstats;

    Data_vehicle vehicles; Data_vehicle *p_vehicles; 
    p_vehicles = &vehicles;

    // String s id
    string account_ids_string;

    // undordered map pre account_id, wn8
    wn8value wn8; wn8value *p_wn8;
    p_wn8 = &wn8;

    //
    GetPlayers(p_aids); // Ziskam id hracov
    cout << "Pocet hracov na spracovanie: " << aids.size() << endl;

    // Triedy na stahovanie dat zo servera
    GetPlayersStats *PlayerS        = new GetPlayersStats;
    GetPlayersVehicles *Vehicles    = new GetPlayersVehicles;

    GetPlayersStats P;
    
    while( !aids.empty() ) 
    {
        // Ziska 100 id ako string
        account_ids_string = GetPlayersIds(p_aids);

        // Nahram 100 statistik hracov        
        PlayerS->Gets( account_ids_string, p_pstats);
        Vehicles->Gets( account_ids_string, p_vehicles);

        // Ziskanie WN8 hodnot
        if(pstats.size() > pamataj) 
        {
            SpocitajWN8(p_pstats, p_vehicles, p_wn8);
        }
        
        // Ak je ziskany WN8 hracov viac ako pamataj uloz ich 
        if(wn8.size() > pamataj)
        {
            UlozHraca(p_wn8);
        }

        account_ids_string.clear();
    }
    
    // Ak nieco zostalo, tak to doukladaj
    if(wn8.size() > 1)
    {
        UlozHraca(p_wn8);
    }


    timestamp_t t2 = get_timestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    return 0;
}
