#include <iostream>
#include <libpq-fe.h>
#include <queue>
#include <chrono>
#include <mutex>
#include <thread>
#include <chrono>

#include "./lib/PlayersVehiclesStat/GetAccountId.h"
#include "./lib/PlayersVehiclesStat/SaveJsons.h"
#include "./lib/PlayersVehiclesStat/Calculate.h"
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"


/** Kontajner na account_id a textove stringy */
std::queue<int> ids;
std::queue<std::string> jsons;

/** Co budem pozadovat zo serveru */
std::string field = "&fields=stronghold_skirmish.battles,stronghold_skirmish.wins,stronghold_skirmish.damage_dealt,stronghold_skirmish.spotted,stronghold_skirmish.frags,stronghold_skirmish.dropped_capture_points,"
                    "globalmap.battles,globalmap.wins,globalmap.damage_dealt,globalmap.spotted,globalmap.frags,globalmap.dropped_capture_points,"
                    "stronghold_defense.battles,stronghold_defense.wins,stronghold_defense.damage_dealt,stronghold_defense.spotted,stronghold_defense.frags,stronghold_defense.dropped_capture_points,"
                    "all.battles,all.wins,all.damage_dealt,all.spotted,all.frags,all.dropped_capture_points,tank_id,mark_of_mastery";

std::string field_only_all = "&fields=all.battles,all.wins,all.damage_dealt,all.spotted,all.frags,all.dropped_capture_points,tank_id,mark_of_mastery";


/** Metoda ktorou sa budem dotazova */
const string method = "/tanks/stats/";

/** Zamok pre kriticku oblast json */
mutex id_lock;
mutex json_lock;

/** Ziskaj data zo servera a uloz ich do docasneho suboru */
void GetDataFromSever()
{
    std::string json_string;
    
    if (ids.size() > 0)
    {
        id_lock.lock();
            int id = ids.front();
            ids.pop();
        id_lock.unlock();

        std::string post_data = field_only_all + "&account_id=" + to_string(id);

        SendCurl send;
        
        try
        {                
            json_string = send.SendWOT(method, post_data);  

            json_lock.lock();
                jsons.push(json_string);
            json_lock.unlock();

            post_data.clear();            
            json_string.clear();            
            
        }
        catch (string e)
        {            
            std::cout << "error: " << e << std::endl;
            ids.push(id);
        }
    }
    
    json_string.clear();
}

void clearQueue(std::queue<std::string> &q)
{
    std::queue<std::string> empty;
    std::swap(q, empty);
}

class Config
{
public:
    static std::string getQuery()
    {
        return "SELECT account_id FROM nabor_new";
    }

    static const uint pocet_json = 89;

    static const int sleep_duration = 500;    
};

int main()
{
    // Ziskaj account_ids
    GetAccountId *getIds = new GetAccountId(Config::getQuery());
    ids = getIds->dataset();    
    printf("Pocet hracov na spracovanie: %i \n", getIds->rowCount());
    delete getIds;

    //Ziskaj json from server
    while (ids.size() > 0)
    {
        thread t1(GetDataFromSever);
        thread t2(GetDataFromSever);
        thread t3(GetDataFromSever);
        thread t4(GetDataFromSever);
        thread t5(GetDataFromSever);
        thread t6(GetDataFromSever);
        thread t7(GetDataFromSever);
        thread t8(GetDataFromSever);
        thread t9(GetDataFromSever);
        thread t10(GetDataFromSever);
        thread t11(GetDataFromSever);
        thread t12(GetDataFromSever);
        thread t13(GetDataFromSever);
        thread t14(GetDataFromSever);
        thread t15(GetDataFromSever);

        if (jsons.size() > Config::pocet_json)
        {
            /**
            SaveJsons *save = new SaveJsons(jsons);
            delete save;
            clearQueue(jsons);
            */
            Calculate calculate;
            calculate.set(jsons);

            clearQueue(jsons);
        }

        t1.join();
        t2.join();
        t3.join();
        t4.join();
        t5.join();
        t6.join();
        t7.join();
        t8.join();
        t9.join();
        t10.join();
        t11.join();
        t12.join();
        t13.join();
        t14.join();
        t15.join();        
      
        
        //std::this_thread::sleep_for(std::chrono::milliseconds( 500 ));
        

        std::cout << "\r"
                  << "Zostava na spracovanie: " << ids.size() << std::flush;
    };

    // Dokoncenie posledny zvyskov fronty - menej ako Config::pocet_json
    SaveJsons *save = new SaveJsons(jsons);
    delete save;
    clearQueue(jsons);

    return 0;
}