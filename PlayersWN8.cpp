#include <iostream>
#include <string>
#include <libpq-fe.h>
#include <sys/time.h>
#include "./lib/Pgsql.h"
#include <unordered_map>
#include <algorithm>



using namespace std;

/* Meranie casu */
typedef unsigned long long timestamp_t;

/* Container na account_id */
typedef unordered_map<int,int> mymap;

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

void GetPlayers(mymap *a_ids, int *ntuples)
{
    PGconn *conn;
    PGresult *result;
    const char query[] = "SELECT account_id FROM players_all";

    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();
    

    result = PQexec(conn,query);

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            cout << "Chyba v GetPlayers: " <<  PQresultErrorMessage(result) << endl;
            fprintf(stderr, "SET failed: %s", PQerrorMessage(conn));
            PQclear(result);

        }
    
    *ntuples = PQntuples(result);

    int i;
    for(i = 0; i < *ntuples; i++)
    {
        (*a_ids)[i]  = stoi (PQgetvalue(result,i,0));
    }
    

    PQclear(result);
    PQfinish(conn);
    //delete pgsql;
}

void PreparedStatmentPlayersStat(PGconn *conn)
{
    PGresult *result;
    const char statment[] = "PlayersStat";
    const char query[]  = "SELECT damage_dealt,spotted,frags,dropped_capture_points,wins,battles FROM ps_all WHERE account_id = $1 ";

    result = PQprepare(conn,statment,query,1,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cout << "Prepared statment PlayersStat je chybny: " << PQerrorMessage(conn) << endl;
        PQclear(result);
    }
    PQclear(result);
}

void ExecPSPlayersStat(PGconn *conn, int account_id, int * pointer)
{
    PGresult *result;
    string aid = to_string(account_id);

    const char statm[] = "PlayersStat";
    const char *paramValues[1];
    paramValues[0] = aid.c_str();

    /* SELECT damage_dealt,spotted,frags,dropped_capture_points,wins FROM players_stat_all WHERE account_id = $1 */
    result  = PQexecPrepared(conn,statm,1,paramValues,NULL,NULL,0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {cout << "Chyba v select statment PlayersStat: " <<  PQresultErrorMessage(result) << endl;}

    int count = PQntuples(result);

    if(count == 1)
    {
        *(pointer+0) = stoi(PQgetvalue(result,0,0)); // dmg
        *(pointer+1) = stoi(PQgetvalue(result,0,1)); // spot
        *(pointer+2) = stoi(PQgetvalue(result,0,2)); // frag
        *(pointer+3) = stoi(PQgetvalue(result,0,3)); // def
        *(pointer+4) = stoi(PQgetvalue(result,0,4)); // win
        *(pointer+5) = stoi(PQgetvalue(result,0,5)); // battles
    }
    
    PQclear(result);
}

void ExecPSPVS(PGconn *conn, int account_id, mymap *pointer)
{
    string aid = to_string(account_id);

    PGresult *result;
    const char statm[] = "pvs_all";
    const char *paramValues[1];
    paramValues[0] = aid.c_str();

    /* SELECT tank_id,battles FROM pvs_all WHERE account_id = $1 */
    result  = PQexecPrepared(conn,statm,1,paramValues,NULL,NULL,0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {cout << "Chyba v select statment pvs_all: " <<  PQresultErrorMessage(result) << endl;}

    int ntuples = PQntuples(result);
    int i;

    for(i = 0; i < ntuples; i++)
    {
        int tank_id = stoi(PQgetvalue(result,i,0));
        int battles = stoi(PQgetvalue(result,i,1));

        (*pointer)[tank_id]  = battles;        

    }
    PQclear(result);
}


void PreparedStatmentPlayersVehicleStat(PGconn *conn)
{
    PGresult *result;
    const char statment[] = "pvs_all";
    const char query[]  = "SELECT tank_id,battles FROM pvs_all WHERE account_id = $1 ";

    result = PQprepare(conn,statment,query,1,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cout << "Prepared statment pvs_all je chybny: " << PQerrorMessage(conn) << endl;
        
    }
    PQclear(result);
}


void PreparedStatmentETV(PGconn *conn)
{
    PGresult *result;
    const char statment[] = "etv";
    const char query[]  = "SELECT dmg,spot,frag,def,win FROM expected_tank_value WHERE tank_id = $1 ";

    result = PQprepare(conn,statment,query,1,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        cout << "Prepared statment etv je chybny: " << PQerrorMessage(conn) << endl;
        
    }
    PQclear(result);
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


        float GetWN8(int *PlayerData, mymap *TankData)
        {
            float wn8 = 0;
            wn8  = this->Calculate(PlayerData,TankData);
            return wn8;
        }
    private:
        etv_table etv; // unordered_map pre etv tabulku
        etv_data  data; // struktura ktora je v  unordered_map pod klucom tank_id

        float Calculate(int *PlayerData, mymap *TankData)
        {
            double expDMG,expSpot,expFrag,expDef,expWin;
	    expDMG = expSpot = expFrag = expDef = expWin = 0.00;
            etv_data tmp;

            int tank_id,battles;

            for (auto& x: *TankData)
            {
                tank_id = x.first;
                battles = x.second;

                tmp = this->etv[tank_id];

                expDMG      += tmp.dmg  * (double)battles;
                expSpot     += tmp.spot * (double)battles;
                expFrag     += tmp.frag * (double)battles;
                expDef      += tmp.def  * (double)battles;
                expWin      += 0.01 * tmp.win * (double)battles;


            }

            double rDMG,rSpot,rFrag,rDef,rWin;
	    rDMG = rSpot = rFrag = rDef = rWin = 0.00;

            rDMG    = PlayerData[0]  / expDMG;
            rSpot   = PlayerData[1]  / expSpot;
            rFrag   = PlayerData[2]  / expFrag;
            rDef    = PlayerData[3]  / expDef;
            rWin    = PlayerData[4]  / expWin;

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

void UlozHraca(PGconn *conn, int account_id, float wn8player)
{
    string Datum();
    string date = Datum();
    PGresult *result, *result2;
    int ntuples, ntuples2;

    string q1 = "SELECT account_id FROM wn8player_history WHERE account_id = "+to_string(account_id)+" and date = current_date - interval '1 days'";
    string q2 = "SELECT account_id FROM wn8player WHERE account_id="+to_string(account_id);

    result = PQexec(conn, q1.c_str());
    result2= PQexec(conn, q2.c_str());

    ntuples = PQntuples(result);
    ntuples2= PQntuples(result2);

    PQclear(result);

    // Pracuj s tabulkou wn8player
    if(ntuples2 == 0)
    {

        if(wn8player > 5)
        {
            string insert2 = "INSERT INTO wn8player (account_id,wn8) VALUES ("+to_string(account_id)+","+to_string(wn8player)+")";
            result = PQexec(conn,insert2.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
                            {cout << "insert wn8player je chybny: " <<  PQresultErrorMessage(result) << endl;}

            PQclear(result);
        }

    }
    else
    {
        string update2  = "UPDATE wn8player SET wn8 = "+to_string(wn8player)+" WHERE account_id = "+to_string(account_id);
        result = PQexec(conn,update2.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
                        {cout << "update wn8player je chybny: " <<  PQresultErrorMessage(result) << endl;}

        PQclear(result);
    }


    // Pracuj s tabulkou wn8player_history
    if(ntuples == 0)
    {

        if(wn8player > 10)
        {
            string insert  = "INSERT INTO wn8player_history (account_id,wn8,date) VALUES ("+to_string(account_id)+","+to_string(wn8player)+", current_date - interval '1 days')";

            result = PQexec(conn,insert.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
                            {cout << "insert into wn8player_history je chybny: " <<  PQresultErrorMessage(result) << endl;}

            PQclear(result);
        }

    }

    PQclear(result2);
}

int main()
{
    /* Informacia o case zacatia programu */
    time_t start, stop;
    timestamp_t t0 = get_timestamp();
    time(&start);
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;

    mymap aids; mymap *p_aids; p_aids = &aids;
    int riadkov; int *p_riadkov; p_riadkov =&riadkov;

    GetPlayers(p_aids, p_riadkov); // Ziskam account_id hracov u ktorych budem zistovat WN8 a pocet riadkov

    // Spojenie do databazy
    PGconn *conn;
    Pgsql *pgsql    = new Pgsql;
    conn            = pgsql->Get();
    //delete pgsql;

    // Pripravim si dotaz do players_stat_all
    PreparedStatmentPlayersStat(conn);
    // Pripravim si dotaz do pvs_all
    PreparedStatmentPlayersVehicleStat(conn);

    int player_stat[6]; int *pointer; pointer = player_stat; // pole s hracovimi statmi a pointer k tomu
    void ExecPSPlayersStat(PGconn *conn, int account_id, int *player_stat);

    mymap tstats; mymap *p_tstats; p_tstats = &tstats;  // map s tank_id a battles
    void ExecPSPVS(PGconn *conn, int account_id, mymap *pointer);

    void UlozHraca(PGconn *conn, int account_id, float wn8player);

    float wn8player;
    WN8Player *wn8 = new WN8Player;

    int i;

    for(i = 0; i < riadkov; i++)
    {
        ExecPSPlayersStat(conn, aids[i], pointer);
        ExecPSPVS(conn, aids[i],p_tstats);

        if( !tstats.empty() ) {
            wn8player = wn8->GetWN8(pointer,p_tstats);
            UlozHraca(conn,aids[i], wn8player);

            wn8player = 1;
        }
	    
        
        player_stat[0] = player_stat[1] = player_stat[2] = player_stat[3] = player_stat[4] = player_stat[5] = 0;
    }




    PQfinish(conn);

    timestamp_t t2 = get_timestamp();
    double secs = (t2 - t0) / 2000000.0L;
    time(&stop);
    cout << "Cas:\t\t" << secs << endl;
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    return 0;
}
