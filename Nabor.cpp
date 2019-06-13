/*
*   Program na kontrolu volnych Cz/Sk hracov 
*
*   author: Boris Fekar
*   datum: 14.08.2016
*
*/

#include <iostream>
#include <sys/time.h>
#include "./lib/Pgsql.h"
#include <thread>

using namespace std;

typedef unsigned long long timestamp_t;

static timestamp_t get_timestamp ()
{
      struct timeval now;
      gettimeofday (&now, NULL);
      return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}
void Truncate()
{
    const char *sql = "truncate nabor_new";
    Pgsql pgsql;
    pgsql.Query(sql);
}

void Vacuum()
{
    const char *sql = "vacuum full analyze nabor_new";
    Pgsql pgsql;
    pgsql.Query(sql);
}

/* Vyberie zakladne informacie o hracoch */
PGresult *Query1()
{
    Pgsql pgsql;
    
    const char *sql = "select pi.account_id, pa.nickname, pi.global_rating, wn8 from players_info pi left join players_all pa on pa.account_id = pi.account_id left join wn8player wn on wn.account_id = pi.account_id where pa.clan_id = 0 and pi.client_language = 'cs' and wn8 > 800";
    return pgsql.Query(sql);
}

void UlozZakladHracov()
{
    PGresult *Query1();
    PGresult *zaklad;
    string insert = "insert into nabor_new (account_id,account_name,global_rating,wn8) VALUES ";
    string value,a_id,nick,gr,wn8;
    int riadkov, i;

    zaklad = Query1();
    riadkov = PQntuples(zaklad);
    
    for(i = 0; i < riadkov;  i++)
    {
        a_id = PQgetvalue(zaklad,i,0);
        nick = PQgetvalue(zaklad,i,1);
        gr   = PQgetvalue(zaklad,i,2);
        wn8  = PQgetvalue(zaklad,i,3);

        if(wn8.size() < 1){wn8 = "0";}

        value += "( " + a_id + ",\'" + nick + "\'," + gr + ", " + wn8 + "),";

        a_id.clear();nick.clear();gr.clear();wn8.clear();
    }
    value.erase(value.end()-1); // Vymaze poslednu ciarku
    
    insert = insert + value;
    Pgsql vloz;
    vloz.Query(insert.c_str());
    

}

/* Updatne battles all,skirmish,stronghold */
void Update1()
{
    Pgsql update1;
    PGconn *conn;
    PGresult *data1; 
    string query1 = "select psa.account_id, psa.battles,psa.wins, pss.battles,psd.battles FROM players_stat_all  psa left join players_stat_skirmish pss on psa.account_id = pss.account_id left join players_stat_defense psd on psa.account_id = psd.account_id where psa.account_id IN (select account_id from nabor_new)";
    string account_id,battles_all,wins,skirmish,stronghold,update;
    int riadkov, i;
    string BattlesAll(PGconn *conn, string account_id);
    string Winrate(string battles_all, string wins);

    data1 = update1.Query(query1.c_str());
    riadkov = PQntuples(data1);

    conn = update1.Get();

    
    for(i = 0; i < riadkov; i++)
    {
        account_id  = PQgetvalue(data1,i,0);
        battles_all = PQgetvalue(data1,i,1);
        wins        = PQgetvalue(data1,i,2);
        skirmish    = PQgetvalue(data1,i,3);
        stronghold  = PQgetvalue(data1,i,4);
        
        wins        = Winrate(battles_all, wins);

        update = "UPDATE nabor_new SET battles_all = "+battles_all+ ", winrate = "+ wins+  ", battles_sk = "+ skirmish + ", battles_sr = "+stronghold+" WHERE account_id =" + account_id;
        PQexec(conn,update.c_str());
        update.clear();battles_all.clear();wins.clear();skirmish.clear();stronghold.clear();account_id.clear();
    }
    PQclear(data1);PQfinish(conn);
}
string Winrate(string battles_all, string wins)
{
    int b, w;
    float wr;
    string res;
    b = stoi(battles_all);
    w = stoi(wins);

    wr = (w / (float)b) * 100;

    res = to_string(wr);

    return res;

}

/* Druha faza updatu naboru */
void Update2()
{
    Pgsql pgsql;
    PGconn *conn1,*conn2,*conn3, *update;
    PGresult *data; 
    string query = "select account_id from nabor_new";
    string account_id;string *p_account_id; p_account_id = &account_id;
    string resultx; string *p_resultx; p_resultx = &resultx;
    string result8; string *p_result8; p_result8 = &result8;
    string result6; string *p_result6; p_result6 = &result6;
    string sql_update;

    void LevelX(PGconn *conn, string *account_id, string *resultx);
    void Level8(PGconn *conn, string *account_id, string *result8);
    void Level6(PGconn *conn, string *account_id, string *result6);

    int i,riadkov;
    conn1 = pgsql.Get();conn2 = pgsql.Get();conn3 = pgsql.Get();update = pgsql.Get(); // Ziskam spojenie do databazy
    data    = PQexec(conn1,query.c_str()); // Poslem dotaz do databazy

    riadkov = PQntuples(data); // zistim pocet riadkov



    for(i = 0; i < riadkov; i++) // zacnem prechadzat account_id a ziskavat k nim data 
    {
        *p_account_id  = PQgetvalue(data,i,0);
        

        thread T1(LevelX,conn1,p_account_id,p_resultx);
        thread T2(Level8,conn2,p_account_id,p_result8);
        thread T3(Level6,conn3,p_account_id,p_result6);

        T1.join();
        T2.join();
        T3.join();

        sql_update = "UPDATE nabor_new SET pocet_x = " + resultx + ", pocet_8 = " + result8 + ", pocet_6 = " + result6 + " WHERE account_id = " + account_id;
        PQexec(update, sql_update.c_str());
        sql_update.clear();resultx.clear();result8.clear();result6.clear();account_id.clear();

    }
    PQclear(data);PQfinish(conn1);PQfinish(conn2);PQfinish(conn3);

}


/* Spocita hracove desiny */
void LevelX(PGconn *conn, string *account_id, string *resultx)
{
    string sql  = "select count(tank_id) from pvs_all where tank_id IN (select tank_id from encyclopedia_vehicles where level = 10) and account_id = ";
    PGresult *res;

    sql = sql + *account_id;

    res = PQexec(conn, sql.c_str());
    *resultx = PQgetvalue(res,0,0);
    PQclear(res);sql.clear();
}

void Level8(PGconn *conn, string *account_id, string *result8)
{
    string sql  = "select count(tank_id) from pvs_all where tank_id IN (select tank_id from encyclopedia_vehicles where level = 8) and account_id = ";
    PGresult *res;

    sql = sql + *account_id;

    res = PQexec(conn, sql.c_str());
    *result8 = PQgetvalue(res,0,0);
    PQclear(res);sql.clear();
}

void Level6(PGconn *conn, string *account_id, string *result6)
{
    string sql  = "select count(tank_id) from pvs_all where tank_id IN (select tank_id from encyclopedia_vehicles where level = 6) and account_id = ";
    PGresult *res;

    sql = sql + *account_id;

    res = PQexec(conn, sql.c_str());
    *result6 = PQgetvalue(res,0,0);
    PQclear(res);
}

string BattlesAll(PGconn *conn, string account_id)
{
    PGresult *res;
    string result;
    string sql = "select battles from players_stat_all where account_id = " + account_id;
    
    res = PQexec(conn, sql.c_str());
    result = PQgetvalue(res,0,0); 
    PQclear(res);
    return result;
}

/* Kontrola kolko hrac odohral bitiek za 7,14,30 dni */
void Update3()
{
    Pgsql pgsql;
    PGconn *conn;
    PGresult *data, *result; 
    string query = "select account_id from nabor_new";  
    string account_id;string *p_account_id; p_account_id = &account_id;
    string data7;   string *p_data7;    p_data7 = &data7;
    string data14;  string *p_data14;   p_data14 = &data14;
    string data30;  string *p_data30;   p_data30 = &data30;  
    string avgxp;   string *p_avgxp;    p_avgxp = &avgxp;
    string sql_update;

    conn = pgsql.Get();
    data    = PQexec(conn,query.c_str()); // Poslem dotaz do databazy
    int riadkov = PQntuples(data); // zistim pocet riadkov
    
    void History(string *p_account_id, PGconn *conn, string *p_data7, string *p_data14, string *p_data30,string *p_avgxp);

    int i;
    for(i=0; i < riadkov;i++)
    {
        account_id = PQgetvalue(data,i,0);
        History(p_account_id, conn, p_data7,p_data14,p_data30,p_avgxp);

	if(data7.length() > 1 && data14.length() > 1 &&  data30.length() > 1)
	{
          sql_update = "UPDATE nabor_new SET last7="+data7+",last14="+data14+",last30="+data30+",avgxp=" + avgxp + " WHERE account_id = " + account_id;
        
          result = PQexec(conn, sql_update.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba update dni: " <<  PQresultErrorMessage(result) << endl;}
	}
        sql_update.clear();account_id.clear();data7.clear();data14.clear();data30.clear();
    }

    PQclear(data);PQfinish(conn);
}

void History(string *p_account_id, PGconn *conn, string *p_data7, string *p_data14, string *p_data30,string *avgxp)
{
    string query7  = "select sum(battles) from players_stat_all_history where date > now() - interval \'7 day\' and account_id = " + *p_account_id;
    string query14 = "select sum(battles) from players_stat_all_history where date > now() - interval \'14 day\' and account_id = " + *p_account_id;
    string query30 = "select sum(battles) from players_stat_all_history where date > now() - interval \'30 day\' and account_id = " + *p_account_id;
    string queryxp   = "select battle_avg_xp from players_stat_all where account_id = " + *p_account_id;

    PGresult *result;

    result  = PQexec(conn,query7.c_str());
         if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "Chyba query7: " <<  PQresultErrorMessage(result) << endl; }
    
    *p_data7 = PQgetvalue(result,0,0);
    
    PQclear(result);

    result  = PQexec(conn,query14.c_str());
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "Chyba query14: " <<  PQresultErrorMessage(result) << endl;}
    *p_data14 = PQgetvalue(result,0,0);
    PQclear(result);

    result  = PQexec(conn,query30.c_str());
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "Chyba query30: " <<  PQresultErrorMessage(result) << endl;}
    *p_data30 = PQgetvalue(result,0,0);
    PQclear(result);

    result  = PQexec(conn,queryxp.c_str());
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "Chyba queryxp: " <<  PQresultErrorMessage(result) << endl;}
    *avgxp = PQgetvalue(result,0,0);
    PQclear(result);

}


int main()
{
    timestamp_t t0 = get_timestamp();
    /* Informacia o case zacatia programu */
    time_t start, stop;

    time(&start);
    cout << "Program zacal pracovat: " << ctime(&start) << endl;
    
    /* Vymaze stare udaje z tabulky nabor_new */
    void Truncate();
    Truncate();

    /* Ulozi do tabulky zaklad hracov account_id,account_name,wn8,global_rating */
    void UlozZakladHracov();
    UlozZakladHracov();
    
    void Update1();
    Update1();

    void Update2();
    Update2();
    
    void Update3();
    Update3();

    // Vacuum tabulky nabor_new
    void Vacuum();
    Vacuum();

    time(&stop);
    timestamp_t t1 = get_timestamp();
    double secs = (t1 - t0) / 1000000.0L;
    cout << "Cas:\t\t\t" << secs << endl;
    cout << "Program skoncil:\t" << ctime(&stop) << endl;        
    return 0;
}
