#include <iostream>
#include <thread>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <fstream>
#include "./json/src/json.hpp"

#include "./lib/Pgsql.h"
#include "./lib/SendCurl.h"

/** Kontainer **/
#include <queue>
#include <map>

using namespace std;

struct player {
  int account_id ;
  string client_language;
  int created_at ;
  int last_battle_time;
};

player player_data;

queue<int> id;
typedef map<int,player> mymap;
mymap data;

int pocet = 0;

// Zamok pre kriticku cast
mutex mtx;
mutex filetx;

// Pripojenie k databaze
PGconn *conn;

PGconn* Spojenie()
{
  Pgsql *pg = new Pgsql;
  conn = pg->Get();

  return conn;
}

// Naplni Frontu FIFO int o dlzke 9 znakov
void NaplnFifo()
{
  // 504279883 504000000
  int from = 504000000; // 9 - cifier
  int to   = 549400000; // 9 - cifier

  for(; from < to; from++)
  {
    id.push(from);
  }
}

int GetIdFIFO()
{
  int i = 0;

  if(!id.empty())
  {
    mtx.lock(); // zamok pre vyberanie ID z fronty
      i = id.front();
      id.pop();
      pocet ++;
    mtx.unlock(); // uvolnenie zamku
  }

  return i;
}


void PripravDavku()
{
  int i = 0;
  int id;

  string davka;
  
  for (i = 0; i < 100; i++)
  {
      id = GetIdFIFO();
      davka += to_string(id) + ",";
  }

  void SendPost(string data);
  SendPost(davka);

}



void SendPost(string id)
{
   
    const char text[] = "&fields=account_id,client_language,created_at,last_battle_time&language=cs";
    string field(text);

    const string method   = "/account/info/";

    string post_data = field  + "&account_id="+ id;

    SendCurl send;
   
    string json = send.SendWOT(method, post_data);

    void SpracujData(string dat);

    filetx.lock();
    SpracujData(json);
    filetx.unlock();

}


void SpracujData(string dat)
{
   void UlozDoDatabazy(int account_id, string client_language, int created_at, int last_battle_time);
   using json = nlohmann::json;
   json js = json::parse(dat);

   string status = js["status"].get<string>();
    
   if(status.compare("ok") == 0)
   {
        json udaje = js["data"];
        for (auto& x : json::iterator_wrapper(udaje))
        {
            
            if(x.value() != NULL)
            {
                json j = x.value();
                
                if(!j.is_null())
                {
                    player_data.account_id = j["account_id"].get<int>();
                    player_data.client_language = j["client_language"].get<string>();
                    player_data.created_at = j["created_at"].get<int>();
                    player_data.last_battle_time = j["last_battle_time"].get<int>();

                    data[j["account_id"].get<int>()] = player_data;
                }
            }

        }

   }

  
}

string NazovTabulky(int account_id)
{
    
    if(account_id < (504000000 + 1 * 5045000)) {
        return "newp_1";
    }
    else if (account_id < (504000000 + 2 * 5045000)) {
        return "newp_2";
    }
    else if (account_id < (504000000 + 3 * 5045000)) {
        return "newp_3";
    }
    else if (account_id < (504000000 + 4 * 5045000)) {
        return "newp_4";
    }
    else if (account_id < (504000000 + 5 * 5045000)) {
        return "newp_5";
    }
    else if (account_id < (504000000 + 6 * 5045000)) {
        return "newp_6";
    }
    else if (account_id < (504000000 + 7 * 5045000)) {
        return "newp_7";
    }
    else if (account_id < (504000000 + 8 * 5045000)) {
        return "newp_8";
    }
    else if (account_id < (504000000 + 9 * 5045000)) {
        return "newp_9";
    }
    else if (1) {
        return "newp_10";
    }

}


void UlozDoDatabazy()
{
    PGresult *result;
    string NazovTabulky(int account_id);
    string dataset, table;
    int i = 0;

    map<int,player>::iterator it;
    for(it = data.begin(); it != data.end(); it++ ) 
    {
       if(i == 0) table = NazovTabulky(it->first);

       dataset += "("+to_string(it->second.account_id)+",'"+it->second.client_language+"',to_timestamp("+to_string(it->second.created_at)+"), to_timestamp("+to_string(it->second.last_battle_time)+")),";
       data.erase(it->second.account_id);
       i++;

    }
    dataset.pop_back();
    string query = "INSERT INTO "+table+" VALUES "+dataset;
 
    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
                {cout << "Chyba INSERT " <<  PQresultErrorMessage(result) << endl;}
}


int main()
{
  NaplnFifo();
  Spojenie();

  int num_thread = 8;
  thread t[num_thread];

      while(id.size() > 0) 
      {
        for(int i = 0; i < num_thread; i++)
        {
          t[i] = thread(PripravDavku);
        }

        for(int i = 0; i < num_thread; i++)
        {
          t[i].join();
        }
      
        if(data.size() > 10000)
        {
          
          UlozDoDatabazy();
      
        }


      }
  cout << "Plnenie skoncene! " << endl;



  map<int,player>::iterator it;
  
  cout << "Pocet opakovani: " << pocet << endl;
  return 0;
}
