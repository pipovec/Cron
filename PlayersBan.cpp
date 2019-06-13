#include <iostream>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"
#include <sys/time.h>
#include <libpq-fe.h>
#include <chrono>

class GetId
{
    public:

    void Spracuj(string *p_js)    
    {
        int i;
        using json = nlohmann::json;
        json js = json::parse(*p_js);

        int pocet = js["meta"]["count"].get<int>();
        
        json data= js["data"];

        json::iterator it = data.begin();

        for(i = 0; i < pocet; i++)
        {
            json player = *it;

            if(player["ban_time"] != NULL)
            {
                cout << "Account_id: "  <<player["account_id"].get<int>() << " Ban info: "  <<player["ban_info"] << " Ban time: "  << player["ban_time"] << endl;
                it++;
            }
        }

        

        

        /*auto nula = data["status"];

        cout << nula << "\n";
*/

    }


    void SendPost(string clan_id, string *json)
    {
        // https://api.worldoftanks.eu/wot/account/info/?application_id=demo&account_id=504279883%2C&fields=ban_time%2Cban_info&language=cs
        const char text[] = "&fields=account_id,ban_time,ban_info&language=cs";
        string field(text);
        const string method   = "/account/info/";

        string post_data = field  + "&account_id="+ clan_id;
        SendCurl send;
        
        *json = send.SendWOT(method, post_data);

    }


    int pocet_riadkov()
    {
            int riadkov;
            
            string query = "SELECT count(*) as pocet FROM players_all ";
            PGconn *conn;
            PGresult *result;

            Pgsql *pg = new Pgsql;
            conn = pg->Get();
            delete pg;

            result = PQexec(conn, query.c_str());
            if (PQresultStatus(result) != PGRES_TUPLES_OK)
                        {cout << "Chyba spocitania hracov " <<  PQresultErrorMessage(result) << endl;}
           

            riadkov =  stoi(PQgetvalue(result,0,0));

            
            PQclear(result);PQfinish(conn);

            return riadkov;

    }


    void get_account_id(string *ids, int offset)
    {
            string query = "SELECT account_id FROM players_all OFFSET " + to_string(offset) + " LIMIT 100";
            PGconn *conn;
            PGresult *result;

            Pgsql *pg = new Pgsql;
            conn = pg->Get();
            delete pg;

            result = PQexec(conn, query.c_str());
             if (PQresultStatus(result) != PGRES_TUPLES_OK)
                        {cout << "Chyba ziskania hracov " <<  PQresultErrorMessage(result) << endl;}
            
            int i;

            for(i = 0; i < 100; i++)
            {
                *ids += PQgetvalue(result,i,0);
                *ids += ",";
                
            }

            PQclear(result);PQfinish(conn);
        }

};



int main()
{
    chrono::time_point<chrono::high_resolution_clock> start, t1, t2;

    int pocet_id; 
    int i;

    start  = chrono::high_resolution_clock::now();

    GetId *id = new GetId();
    pocet_id = id->pocet_riadkov();

    string ids; string *p_ids; p_ids = &ids;
    string js; string *p_json; p_json = &js;
   
   cout << "zacneme makat "<< endl;


   for(i = 0; i < pocet_id; i = i+100)
   {
      
      
       id->get_account_id(p_ids,i);
      

       id->SendPost(ids,p_json);

       id->Spracuj(p_json);
      
      ids.clear();js.clear();

      cout << "\r";
      cout << "Spracovanych \t :" << i << flush;


   }
   
   

    
  t1 = chrono::high_resolution_clock::now();
  chrono::duration<double> elapsed_seconds = t1-start;

  cout << "Dlzka programu:\t" << elapsed_seconds.count() << endl;

    return 0;
}
