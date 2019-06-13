/*
*   Hladanie DE klanov. (Prerobene z CS klanov)
*   Najprv vyberie hracov ktory pouzivaju v hre DE klienta a ich clan_id
*   Potom zacne pozerat jednotlive klany a spocitavat kolko percent hracov
*   v klane pouziva DE klienta
*
*   07.08.2016 Boris Fekar
*/


#include <iostream>
#include <libpq-fe.h>
#include <sys/time.h>
#include <thread>

using namespace std; 

class Databaza
{
    public:
    // Pripojenie k databaze
    PGconn *Connect()
    {
        PGconn  *conn;
        const char *conninfo;
        conninfo = "dbname=clan user=deamon password=sedemtri";

        conn = PQconnectdb(conninfo);
        /* Check to see that the backend connection was successfully made */
        if (PQstatus(conn) != CONNECTION_OK)
        {
        fprintf(stderr, "Connection to database failed: %s",
                PQerrorMessage(conn));
        
        }
        return conn;
    }

    static void exit_nicely(PGconn *conn)
    {
        
    }


    // Vytiahne vsetky clan_id z tabulky clan_all
    PGresult* SelectClanAll()
    {
        PGresult *res;
        PGconn  *conn;

        conn = this->Connect();
       
        const char *sql;
        sql = "SELECT DISTINCT(clan_id) FROM players_all pa LEFT JOIN players_info pi ON pa.account_id = pi.account_id WHERE client_language = 'de'";
        res = PQexec(conn , sql);

        PQfinish(conn);
        return res;
    }

    // Zistim pocet clenov klanu z tabulky clan_all
    PGresult* MembersCount()
    {
        PGresult *res;
        PGconn  *conn;
        
        string string_sql;
        string_sql = "SELECT clan_id, members_count FROM clan_all ";

        const char *sql;
        sql = string_sql.c_str();

        conn = this->Connect();
        res = PQexec(conn , sql);
        PQfinish(conn);
        
        return res;
    }

    // Vyberiem si vsetkych hracov ktory pouzivaju klienta CS v hre a budem ich zratavat v pamati servera
    PGresult* KlientCS()
    {
        PGresult *res;
        PGconn  *conn;
        
        string string_sql;
        string_sql = "SELECT pa.clan_id, pa.account_id FROM players_all pa LEFT JOIN players_info pi ON pa.account_id = pi.account_id  WHERE client_language = 'de' AND pa.clan_id > 0";

        const char *sql;
        sql = string_sql.c_str();

        conn = this->Connect();
        res = PQexec(this->Connect() , sql);
        PQfinish(conn);

        return res;
    }

    void InsertClansCS(string clans)
    {
        PGconn  *conn;
        string sql, truncate, i_clans;
        
        truncate = "TRUNCATE tmp_clans_de";
        sql = "INSERT INTO tmp_clans_de VALUES "+clans ;
        
        
        conn = this->Connect();
        PQexec(conn , truncate.c_str());
        PQexec(conn , sql.c_str());
        PQfinish(conn);
    }




};


// Funkcia ktora spocita v resulte pocet hracov s CS klientom
void SpocitajCSKlientov(PGresult *res, string clan_id, float *klient)
{
    
    float counter = 0;
    int riadkov, i;
    string c_id; 

    riadkov = PQntuples(res); // Pocet riadkov resultu
    
    for(i = 0; i < riadkov; i++)
    {
        c_id = PQgetvalue(res, i, 0);
        if(c_id == clan_id){counter ++;}
    }
   
    *klient = counter;
    
}

// Funkcia najde v pamati udaj s clan_id k nemu prisluchajuci members_count
void ReturnMembersCount(PGresult *res, string clan_id, float *members)
{
    string result, c_id;
    int riadkov, i;

    riadkov = PQntuples(res); // Pocet riadkov resultu

    for(i = 0; i < riadkov; i++)
    {
        c_id = PQgetvalue(res, i, 0);
        if(c_id == clan_id)
        {
            result = PQgetvalue(res, i, 1);
            *members = stof(result); break;
        }
    }

}

/* Meranie casu */
    typedef unsigned long long timestamp_t;

    static timestamp_t
    get_timestamp ()
    {
      struct timeval now;
      gettimeofday (&now, NULL);
      return  now.tv_usec + (timestamp_t)now.tv_sec * 1000000;
    }

int main()
{
    timestamp_t t0 = get_timestamp();

    Databaza baza;
    PGresult *ClanAll;
    ClanAll = baza.SelectClanAll();

    int ntuples, i;
    // Pocet riadkov
    ntuples = PQntuples(ClanAll); // Riadky resultu

    
    PGresult *members_count; PGresult *client_count;
    string clan_id;
    float members, client, percento;
    int pocet_cs_klanov = 0;

    // Natiahnem si do pamate vsetkych hracov a ich clan_id ktory maju DE klienta
    client_count = baza.KlientCS();

    // Nacitam si do pamate vsetky clan_id spolu s members_count z tabulky clan_all 
    members_count = baza.MembersCount();
    
    // Deklarujem funkciu na spocitavnie hracov DE z tabulky players_info
    float SpocitajCSKlientov(PGresult *res, string clan_id, /*out*/ float *klient);

    // Deklarujem funkciu na najdenie members_count podla clan_id
    float ReturnMembersCount(PGresult *res, string clan_id, /*out*/ float *members);

    
    float *ptr_client; float *ptr_members;

    ptr_client = &client; ptr_members = &members;
    
    string cs_clans;
    
    for(i = 0; i < ntuples; i++)
    {
        
        
         clan_id = PQgetvalue(ClanAll, i, 0); // Ziskam clan_id z resultu
         
         
         
         /* Ziskam members_count podla clan_id */
         thread T1(ReturnMembersCount,members_count, clan_id, ptr_members);

         
         
         /* Spocitam pocet hracov ktory hraju s CS klientom v pamati pocitaca*/
         thread T2(SpocitajCSKlientov,client_count, clan_id, ptr_client);

         T1.join();
         T2.join();
         
         percento = ( client / members ) * 100;


         if(percento > 50)
         {
             cs_clans += "("+ clan_id + "),"; 
             ++ pocet_cs_klanov;
         }

         client = 0; members = 0;
    }
    
    cs_clans.erase(cs_clans.end()-1); // Vymaze poslednu ciarku zo stringu
    baza.InsertClansCS(cs_clans);



    timestamp_t t1 = get_timestamp();
    double secs = (t1 - t0) / 1000000.0L;
    cout << endl << "*********** Kontrola DE klanov ****************" << endl;
    cout << "Pocet prehladavanych klanov: \t" << ntuples << endl;
    cout << "Pocet DE klanov: \t\t" << pocet_cs_klanov << endl;
    cout << "Cas: \t \t\t\t" << secs << endl ;
    cout << endl;
    

    return 0;
}
