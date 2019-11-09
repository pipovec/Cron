#include <iostream>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include <sys/time.h>
#include <libpq-fe.h>


/* Meranie casu */
typedef unsigned long long timestamp_t;

static timestamp_t get_timestamp ()
{
      struct timeval now;
      gettimeofday (&now, NULL);
      return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
}

using namespace std;

// Callback odosle data na server WOT a ulozi ho do pointra
string Send(string method, string post) 
{
    SendCurl send;
    string json;
    
    json = send.SendWOT(method, post);    
   
    return json; 
}

/* Parsovanie JSON. Hladanie podla account_id a nasledne vytiahne udaje a spracuje ich pre INSERT do databazy */
string dataHraca(string account_id, string *json)
{
    string tmp;
    
    unsigned first = json->find(account_id); // Najde zaciatok kde sa zacinaju json account_id
    unsigned koniec   = json->find("}", first);
    tmp = json->substr(first+11, (koniec) - (first+11));

    if(tmp.compare(0,4,"null")== 0){tmp = "null";} 

   
    return tmp;
}

// Natiahne account_id do pola a uz nebude traba pgresult */
void Players2(int *p_Array, int *p_riadkov)
{
    PGconn *conn; PGresult *result;
    Pgsql trieda;
    conn = trieda.Get();
    int i; string a_id;int ntuples;

    string sql = "SELECT account_id FROM players_all ORDER BY account_id DESC";

    result = PQexec(conn, sql.c_str());
    ntuples = PQntuples(result);

    cout << "Celkom na spracovanie: " << ntuples << endl;

    for(i = 0; i < ntuples; i++)
    {
        a_id  = PQgetvalue(result, i, 0);
        *(p_Array+i) = stoi(a_id); 
        a_id.clear();
    }

    sql.clear();PQclear(result);PQfinish(conn);a_id.clear();
}

int CountPlayers2()
{
    PGconn *conn; PGresult *result;
    Pgsql trieda;
    conn = trieda.Get();
    int i; string a_id;

    string sql = "SELECT count(account_id) FROM players_all";
    result = PQexec(conn, sql.c_str());

    a_id  = PQgetvalue(result, 0, 0);
    
    i = stoi(a_id);

    a_id.clear();PQclear(result);PQfinish(conn);a_id.clear();
    return i;

}

/* Rozsekam 400 hracov na 100-vky account_id */
string Sekera(int *p_Array, int i, int *ptr_riadkov, int *ptr_sprac)
{
    string account_ids;
    string a_id;
    int ii; int account_id;
    *ptr_sprac = 0;
    ii = i;

    for(; ii < i + 100; ii++)
    {
        if(ii < *ptr_riadkov)
        {
            
            account_id = *(p_Array + ii);
            account_ids +=  to_string(account_id) + ",";

            *ptr_sprac = *ptr_sprac + 1;
        }        
    }
    
    account_ids.erase(account_ids.end()-1); // Vymaze poslednu ciarku
    a_id.clear();
   
    return account_ids;
}

/* Spracuje udaje a ulozi ich do databazy */
void SpracujHracov(int i, int *p_Array, string *json, string a_id100, int *upd, int *ins)
{
    int ii;
    
    PGresult *result;
    string dataHraca(string account_id, string *json);  // funkcia na parsovanie JSON
    void DeletePlayers(PGconn *conn, string a_id); // Vymazanie hraca z databazy
    void UpdatePlayersInfo(PGconn *conn, string account_id, string client_language, string global_rating, string logout_at, string created_at, string last_battle_time); // Update players_info
    void InsertPlayersInfo(PGconn *conn,string account_id, string client_language, string global_rating, string logout_at, string created_at, string last_battle_time );

    //Otvorim si spojenie do databazy 
    PGconn  *conn;
    Pgsql trieda;
    conn = trieda.Get();

    //Prepared statment 
    PQprepare(  conn,
                "players_info",
                "SELECT account_id,client_language,global_rating,extract(epoch from logout_at),extract(epoch from created_at),extract(epoch from last_battle_time) FROM players_info WHERE account_id = $1",
                1,
                NULL
            );
    
    ii = i;

    string client_language;
    string global_rating;
    string logout_at;
    string created_at;
    string last_battle_time;

    string cl, gr, la, ca, lbt;

    for(;ii < i+100; ii++)
    {
        
        string a_id    = to_string(*(p_Array + ii));
        string insert  = dataHraca(a_id, json);
        

        if(insert.compare("null") == 0){DeletePlayers(conn, a_id); *ins = *ins + 1;continue;}
        
        /* client language */
        unsigned zaciatok   = insert.find("client_language"); // 
        unsigned koniec     = insert.find("\",", zaciatok);
        cl                  = insert.substr(zaciatok+18, koniec - (zaciatok+18));
        
        /* global rating */ 
        zaciatok            = insert.find("global_rating");
        koniec              = insert.find(",", zaciatok);
        gr                  = insert.substr(zaciatok+15, koniec - (zaciatok+15)); 

        /* logout_at */ 
        zaciatok            = insert.find("logout_at");
        koniec              = insert.find(",", zaciatok);
        la                  = insert.substr(zaciatok+11, koniec - (zaciatok+11));

        /* created_at */ 
        zaciatok            = insert.find("created_at");
        koniec              = insert.find(",", zaciatok);
        ca                  = insert.substr(zaciatok+12, koniec - (zaciatok+12));

        /* last_battle_time */ 
        zaciatok            = insert.find("last_battle_time");
        koniec              = insert.find("}", zaciatok);
        lbt                 = insert.substr(zaciatok+18, koniec - (zaciatok+18));
        
        // Porovnanie s databazou 
        const char *paramValues[1];
        paramValues[0] = a_id.c_str();
        result  = PQexecPrepared(conn,"players_info",1,paramValues,NULL,NULL,0);
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "players_info prepared statment je chybny: " <<  PQerrorMessage(conn) << endl;}
        
        int ntuples = PQntuples(result); 

        if(ntuples == 1)
        {
            string client_language  = PQgetvalue(result,0,1);
            string global_rating    = PQgetvalue(result,0,2);
            string logout_at        = PQgetvalue(result,0,3);
            string created_at       = PQgetvalue(result,0,4);
            string last_battle_time = PQgetvalue(result,0,5);

            if(client_language.compare(cl) !=0){   UpdatePlayersInfo(conn,a_id, cl, gr, la, ca, lbt); *upd = *upd + 1; continue;}
            if(global_rating.compare(gr) !=0){    UpdatePlayersInfo(conn,a_id, cl, gr, la, ca, lbt); *upd = *upd + 1; continue;}
            if(logout_at.compare(la) !=0){   UpdatePlayersInfo(conn,a_id, cl, gr, la, ca, lbt); *upd = *upd + 1;continue;}
            if(last_battle_time.compare(lbt) !=0){ UpdatePlayersInfo(conn,a_id, cl, gr, la, ca, lbt);*upd = *upd + 1; continue;}

            
        }
        else
        {
            InsertPlayersInfo(conn,a_id,cl,gr,la,ca,lbt);
        }
        a_id.clear();client_language.clear();global_rating.clear();logout_at.clear();created_at.clear();last_battle_time.clear();
        cl.clear();gr.clear();la.clear();ca.clear();lbt.clear();
        PQclear(result);
    }
    
  PQfinish(conn);  
    
}

void UpdatePlayersInfo(PGconn *conn, string account_id, string client_language, string global_rating, string logout_at, string created_at, string last_battle_time)
{
    string sql = "UPDATE players_info SET client_language = '" + client_language + "', global_rating = " + global_rating + ", logout_at = to_timestamp(" + logout_at + "), created_at = to_timestamp("+created_at+"),";
    sql += " last_battle_time = to_timestamp("+last_battle_time+") WHERE account_id  = " + account_id;

    PGresult *result;
    result = PQexec(conn, sql.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba update players_info "  <<  PQresultErrorMessage(result) << endl;} 

    PQclear(result);
    sql.clear();
   
}


void DeletePlayers(PGconn *conn, string a_id)
{
    string sql     = "DELETE FROM players_all WHERE account_id = " + a_id;
    /* Tabulka players uz nie je. DEPRECATED
    string sql2    = "DELETE FROM players WHERE account_id = " + a_id;
    */
    PGresult *result;
    
    result = PQexec(conn, sql.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba delete players_all "  <<  PQresultErrorMessage(result) << endl;} 
    PQclear(result);

    /*
    result = PQexec(conn, sql2.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba delete players "  <<  PQresultErrorMessage(result) << endl;} 
    PQclear(result);
    */

    sql.clear();
    
    /*sql2.clear();*/
}

void InsertPlayersInfo(PGconn *conn,string account_id, string client_language, string global_rating, string logout_at, string created_at, string last_battle_time )
{
    PGresult *result;

    string insert = "INSERT INTO players_info VALUES ("+account_id+","+global_rating+",'"+client_language+"',to_timestamp("+logout_at+"),to_timestamp("+last_battle_time+"),to_timestamp("+created_at+"))";

    result = PQexec(conn, insert.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {cout << "Chyba insert do players_info "  <<  PQresultErrorMessage(result) << endl;} 
    PQclear(result);
}  


int main()
{
    timestamp_t t0 = get_timestamp();
    /* Informacia o case zacatia programu */
    time_t start, stop;
    
    time(&start);
    cout << "Program zacal o: " << ctime(&start) << endl;

    void Players2(int *p_Array, int *ptr_riadkov);int CountPlayers2();
    string Send(string method, string post);
    void SpracujHracov(int i, int *p_Array, string *json, string a_id,int *upd, int *ins);
    
    /* Zistim pocet riadkov na spracovanie */    
    int riadkov;
    int *ptr_riadkov;
    ptr_riadkov = &riadkov;
    riadkov = CountPlayers2();

    /* Vytvorim si pole kde dam account_id nacitane z databazy */
    int Array[riadkov];
    int *p_Array;
    p_Array = Array;

    Players2(p_Array, ptr_riadkov);

       
    // Co budem pozadovat od serveru
    const string field = "&fields=client_language,global_rating,created_at,logout_at,last_battle_time";
    const string method   = "/account/info/";

    string a_id;
    int sprac; int *p_sprac;
    p_sprac = &sprac;


   /* Zacnem vyberat hracov po 100 ks */
    int i,spracovanych,upd,ins; spracovanych = 0;
    upd = 0; ins = 0;
    int *p_upd, *p_ins; p_upd = &upd; p_ins = &ins;
    string json; string *p_json; p_json = &json;string post;
    
    for(i = 0; i < riadkov; i = i + 100)
    {
        
            a_id = Sekera(p_Array,i,ptr_riadkov,p_sprac);
            
            /* POST data pre poslanie na server */
            post = field+"&account_id="+a_id; 
            
            json = Send(method,post); 
                        
            SpracujHracov(i, p_Array, p_json, a_id,p_upd,p_ins);

            a_id.clear();post.clear(); json.clear();
            spracovanych += sprac; 
            //cout << "Spracovanych: " << spracovanych << "  update: " << upd << "  delete: " << ins << endl;
   }
    
   
    timestamp_t t1 = get_timestamp();

    double secs = (t1 - t0) / 1000000.0L;
   
    cout << "Cas:\t\t" << secs << endl;
    cout << "Celkom:\t\t" << riadkov << endl;
    cout << "Spracovanych:\t" << spracovanych << endl;
    cout << "Vymazanych:\t" << ins << endl;
    cout << "Update:\t" << upd << endl;

    time(&stop);
    cout << endl << "Program skoncil o: " << ctime(&stop) << endl << endl;

    return 0;
    
}
