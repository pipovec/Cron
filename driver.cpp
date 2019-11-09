#include <iostream>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include <sys/time.h>
#include <thread>
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
void Send(string method, string post, int stovka,int i, int *p_Array, int *ptr_riadkov) 
{
    SendCurl *send = new SendCurl;
    string *p_json;
    
   
    p_json = new string(send->SendWOT(method, post));
    delete send;  
   
    /* Spracovanie hracov */
    void SpracujHracov(int stovka, int i, int *p_Array, string *json, int *ptr_riadkov);
    SpracujHracov(stovka, i, p_Array, p_json, ptr_riadkov);

    delete p_json; 
}

/* Parsovanie JSON. Hladanie podla account_id a nasledne vytiahne udaje a spracuje ich pre INSERT do databazy */
string dataHraca(string account_id, string *json)
{
    string tmp;
    string insert;

    insert = "(" + account_id;
    
    unsigned first = json->find(account_id); // Najde zaciatok kde sa zacinaju json account_id
    unsigned koniec   = json->find("}", first);
    tmp = json->substr(first+11, (koniec + 1) - (first+11));

    //cout << "Account_id: " << account_id << "  vstupne data:" << tmp << endl;
    
    if(tmp.compare("null}") == 0)
    {
        return " ";
    }

    /* client language */
    unsigned zaciatok = tmp.find("client_language"); // 
    koniec   = tmp.find("\",", zaciatok);
    insert  += ",\'" + tmp.substr(zaciatok+18, koniec - (zaciatok+18)) + "\',";
    
    /* global rating */ 
    zaciatok = tmp.find("global_rating");
    koniec   = tmp.find(",", zaciatok);
    insert  += tmp.substr(zaciatok+15, koniec - (zaciatok+15)); 

    /* logout_at */ 
    zaciatok = tmp.find("logout_at");
    koniec   = tmp.find(",", zaciatok);
    insert  +=  ",to_timestamp(" +tmp.substr(zaciatok+11, koniec - (zaciatok+11))+"),";

    /* created_at */
    zaciatok = tmp.find("created_at");
    koniec   = tmp.find(",", zaciatok);
    insert  += "to_timestamp("+tmp.substr(zaciatok+12, koniec - (zaciatok+12))+"),";

   /*  last_battle_time */
    zaciatok = tmp.find("last_battle_time");
    koniec   = tmp.find("}", zaciatok);
    insert  += "to_timestamp("+ tmp.substr(zaciatok+18, koniec - (zaciatok+18))+")),";
 
    //cout << "Vystup: " << insert << endl;

    return insert;
}

// Natiahne account_id do pola a uz nebude traba pgresult */
void Players2(int *p_Array, int *p_riadkov)
{
    PGconn *conn; PGresult *result;
    Pgsql trieda;
    conn = trieda.pgsql;
    int i; string a_id;

    string sql = "SELECT account_id FROM players_all";
    result = PQexec(conn, sql.c_str());

    for(i = 0; i < *p_riadkov; i++)
    {
        a_id  = PQgetvalue(result, i, 0);
        *(p_Array+i) = stoi(a_id); 
    }
    PQclear(result);


}

int CountPlayers2()
{
    PGconn *conn; PGresult *result;
    Pgsql trieda;
    conn = trieda.pgsql;
    int i; string a_id;

    string sql = "SELECT count(account_id) FROM players_all";
    result = PQexec(conn, sql.c_str());

    a_id  = PQgetvalue(result, 0, 0);
    PQclear(result);
    i = stoi(a_id);

    return i;

}

/* Rozsekam 400 hracov na 100-vky account_id */
string Sekera(int stovka, int *p_Array, int i, int *ptr_riadkov, int *ptr_sprac)
{
    string account_ids;
    string a_id;
    int ii; int account_id;
    *ptr_sprac = 0;

    switch(stovka)
    {
        case 100: ii=i;break;
        case 200: ii=i+100;break;
        case 300: ii=i+200;break;
        case 400: ii=i+300;break;
    }
    
    for(; ii < i + stovka; ii++)
    {
        if(ii < *ptr_riadkov)
        {
            //a_id = PQgetvalue(res,ii,0);
            account_id = *(p_Array + ii);
            account_ids +=  to_string(account_id) + ",";
            *ptr_sprac = *ptr_sprac + 1;
        }        
    }
    
    account_ids.erase(account_ids.end()-1); // Vymaze poslednu ciarku
   
    return account_ids;
}

/* Spracuje udaje a ulozi ich do databazy */
void SpracujHracov(int stovka, int i, int *p_Array, string *json, int *ptr_riadkov)
{
    int ii;
    string dataHraca(string account_id, string *json);  // funkcia na parsovanie JSON
     
    string a_id; 
    string insert = "INSERT INTO tmp_pi (account_id,client_language,global_rating,logout_at,created_at,last_battle_time) VALUES "; 

    switch(stovka)
    {
        case 100: ii=i;break;
        case 200: ii=i+100;break;
        case 300: ii=i+200;break;
        case 400: ii=i+300;break;
    }
    
    
    for(;ii < i+stovka; ii++)
    {
        if(ii == *ptr_riadkov-1){break;}
        
        a_id    = to_string(*(p_Array + ii));
        insert += dataHraca(a_id, json);
        
    }
    
    // Odstranenie poslednej ciarky
    unsigned ciarka = insert.find_last_of(",");
    insert.erase(ciarka, 1);

    /* Otvorim si spojenie do databazy */
    void InsertPlayersInfo(PGconn *conn, string insert);
    PGconn *conn;
    Pgsql trieda;
    conn = trieda.pgsql;

    //cout << "Data na insert: " << insert << endl;
    
    InsertPlayersInfo(conn, insert);

}

void InsertPlayersInfo(PGconn *conn, string insert)
{
    PGresult *res;
    res = PQexec(conn, insert.c_str());
    PQclear(res);
    
}  

void createTempTable()
{
    PGconn *conn;
    Pgsql trieda;
    conn = trieda.pgsql;
    string create_table = "CREATE UNLOGGED TABLE tmp_pi (account_id integer NOT NULL,global_rating integer,client_language text,logout_at timestamp without time zone,last_battle_time timestamp without time zone,created_at timestamp without time zone,CONSTRAINT tmp_pi_pkey PRIMARY KEY (account_id))";
    
    PQexec(conn, create_table.c_str());

} 

void dropTempTable()
{
    PGconn *conn;
    Pgsql trieda;
    conn = trieda.pgsql;
    string create_table = "DROP TABLE tmp_pi";
    
    PQexec(conn, create_table.c_str());
}

void UpdateMasterTable()
{   
    PGconn *conn;
    Pgsql trieda;
    conn = trieda.pgsql;
    
    string update = "UPDATE players_info pi SET global_rating = tmp.global_rating,client_language = tmp.client_language,logout_at = tmp.logout_at,last_battle_time = tmp.last_battle_time,created_at = tmp.created_at FROM tmp_pi tmp WHERE pi.account_id = tmp.account_id";
    PQexec(conn, update.c_str());


}

void InsertInMasterTable()
{
    PGconn *conn;
    Pgsql trieda;
    conn = trieda.pgsql;

    string insert = "INSERT INTO players_info (account_id,global_rating,client_language,logout_at,last_battle_time,created_at) SELECT account_id,global_rating,client_language,logout_at,last_battle_time,created_at FROM tmp_pi tmp WHERE tmp.account_id NOT IN (select account_id from players_info)";
    PQexec(conn, insert.c_str());
}

int main()
{
    timestamp_t t0 = get_timestamp();
    /* Informacia o case zacatia programu */
    time_t start, stop;
    
    time(&start);
    cout << "Program zacal pracovat: " << ctime(&start) << endl;


    SendCurl send;
    
    void Players2(int *p_Array, int *ptr_riadkov);int CountPlayers2();
    
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
    string method   = "/account/info/";

    string a_id1,a_id2,a_id3,a_id4;
    int sprac1, sprac2, sprac3, sprac4; int *p_sprac1,*p_sprac2,*p_sprac3,*p_sprac4;
    p_sprac1 = &sprac1;p_sprac2 = &sprac2;p_sprac3 = &sprac3;p_sprac4 = &sprac4;

    string Sekera(int c, int *p_Array, int i, int *riadkov, int *ptr_sprac); // Deklaracie funkcie na rozsekanie hracov po 100
    //void SpracujHracov(int stovka,int i, int *p_Array, string *data, int *riadkov); //Deklaracia funkcie na spracovanie udajov po 100

    int spracovanych;
    spracovanych = 0; 
    int r_sprac;

    cout << "Riadkov celkom: " << riadkov << endl ;
    r_sprac = riadkov / 400;
    cout << "Pocet opakovani: " << r_sprac << endl;
    cout << "To je spracovat: " << r_sprac * 400 << endl; 

    /* Toto tu mam preto lebo neviem ako spracovat pocet id nedelitelne 400 */
    riadkov = r_sprac * 400;

      
    // Vytvorim si docasnu tabulku kde budem ukladat aktualne udaje //
    void createTempTable(); void dropTempTable();
    createTempTable();


   /* Zacnem vyberat hracov po 400 ks */
    int i;
    for(i = 0; i < riadkov; i = i + 400)
    {
        
        {
        
        a_id1 = Sekera(100, p_Array,i,ptr_riadkov,p_sprac1);
        a_id2 = Sekera(200, p_Array,i,ptr_riadkov,p_sprac2);
        a_id3 = Sekera(300, p_Array,i,ptr_riadkov,p_sprac3);
        a_id4 = Sekera(400, p_Array,i,ptr_riadkov,p_sprac4);
        
        /* POST data pre poslanie na server */
        string post1    = field+"&account_id="+a_id1; 
        string post2    = field+"&account_id="+a_id2; 
        string post3    = field+"&account_id="+a_id3;  
        string post4    = field+"&account_id="+a_id4;  
        
        thread T1(Send,method,post1,100,i,p_Array,ptr_riadkov); 
        thread T2(Send,method,post1,200,i,p_Array,ptr_riadkov); 
        thread T3(Send,method,post1,300,i,p_Array,ptr_riadkov);
        thread T4(Send,method,post1,400,i,p_Array,ptr_riadkov);

        T1.join();
        T2.join();
        T3.join();
        T4.join();
        
        a_id1.clear();a_id2.clear();a_id3.clear();a_id4.clear();
        post1.clear();post2.clear();post3.clear();post4.clear();
        
        }

        
        spracovanych = spracovanych + sprac1 + sprac2 + sprac3 + sprac4;
        //cout << "Spracovanych: " << spracovanych <<  " | riadkov je: " << riadkov << endl;
    
        sprac1 =  0;sprac2 =  0;sprac3 =  0;sprac4 =  0; 
        
   }
    /* Updatnem hlavnu tabulku player_info*/
    void UpdateMasterTable();
    UpdateMasterTable();

    /* Vlozim nove zaznamy do tabulky players_info */
    void InsertInMasterTable();
    InsertInMasterTable();

    /* Zmazem docasnu tabulku tmp_pi */
    void dropTempTable();
    dropTempTable();
   
    timestamp_t t1 = get_timestamp();

    double secs = (t1 - t0) / 1000000.0L;
   
    cout << "Cas:\t\t" << secs << endl;
    cout << "Celkom:\t\t" << riadkov << endl;
    cout << "Spracovanych:\t" << spracovanych << endl;

    time(&stop);
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    return 0;
    
}
