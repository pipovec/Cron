#include <iostream>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./lib/Queue.h"
#include "./lib/MembersRole.h"

#include <thread>
#include <sstream>
#include <chrono>
#include <mutex>

/** Kontainer **/
#include <queue>
#include <map>

/** Statistika **/
struct statistika
{
    int new_players_all = 0;
    int new_account_id = 0;
    int change_nick = 0;
    int repair_members_role = 0;
    int new_members_role = 0;
    int change_role = 0;
    int empty_clan = 0;
};

statistika stat;

using namespace std;
typedef queue<int> fronta;

string table = "clan_all";

mutex mutex_lock;

const char text[] = "&fields=clan_id,members_count,members.account_id,members.account_name,members.joined_at,members.role_i18n&language=cs";
const string method = "/clans/info/";

void GetClanId(fronta *p_aids)
{
    string query = "SELECT clan_id FROM clan_all WHERE language = 'cs' or language = 'sk' or language = 'pl' LIMIT 120";

    PGresult *result;
    PGconn *conn;

    Pgsql *pg = new Pgsql;
    conn = pg->Get();

    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        cout << "Chyba ziskania clan_id." << endl;
    }

    cout << "Pocet klanov na spracovanie: " << PQntuples(result) << endl;

    int i;

    for (i = 0; i < PQntuples(result); i++)
    {
        (*p_aids).push(stoi(PQgetvalue(result, i, 0)));
    }
    PQclear(result);
    PQfinish(conn);
}

void SendPost(fronta *p_aids, Queue *queue_for_json)
{
    string clan_ids_string;
    int i = 0;

    mutex_lock.lock();
    while (!(*p_aids).empty())
    {
        int clan_id = (*p_aids).front();
        clan_ids_string += to_string(clan_id) + ",";
        (*p_aids).pop();

        i++;
        if (i == 100)
            break;
    }

    cout << "Velkost fronty: " << queue_for_json->size() << " velkost clan_ids: " << (*p_aids).size() << endl;
    mutex_lock.unlock();

    string field(text);
    string post_data = field + "&clan_id=" + clan_ids_string;

    SendCurl send;
    string json_string = send.SendWOT(method, post_data);
    queue_for_json->push(json_string);

    post_data.clear();
    field.clear();
    clan_ids_string.clear();
}

int main()
{
    time_t startprog; //stopprog;
    time(&startprog);

    cout << "*********************************" << endl;
    cout << endl
         << "Program zacal pracovat: " << ctime(&startprog) << endl;

    chrono::time_point<chrono::high_resolution_clock> start, t1, t2, main1, main2;

    /** Spustim meranie cas */
    start = chrono::high_resolution_clock::now();

    // Sem si ulozim vsetky clan_id
    fronta clan_ids;
    fronta *p_clan_ids;
    p_clan_ids = &clan_ids;    
    GetClanId(p_clan_ids);

    // Fronta pre jsons
    Queue *queue_for_json = new Queue;

    while (!clan_ids.empty())
    {

        thread th1(SendPost, p_clan_ids, queue_for_json);
        thread th2(SendPost, p_clan_ids, queue_for_json);

        th1.join();
        th2.join();
    }

    // Spracovanie json
    MembersRole *spracuj_role = new MembersRole();

    while(!queue_for_json->empty())
    {
        spracuj_role->spracuj(queue_for_json->front());
        
    }



    return 0;

    /**
    string json;
    string *p_json;
    p_json = &json;
    string clan_id;
    string *p_clan_id;
    p_clan_id = &clan_id;
    int a_clan_id[100];
    int i = 0;
    while (!cids.empty())
    {
        main1 = chrono::high_resolution_clock::now();
        i++;

        Get100Id(p_clan_id, p_cids, a_clan_id); // Ziskam string pre poslanie na server a pole intov na dalsie spracovanie

        SendPost(clan_id, p_json); // Poslem udaje na server a vyzdvihnem cerstvy material

        Spracuj *doit = new Spracuj(a_clan_id, json, permanent_connection);

        clan_id.clear();
        json.clear();
        CleaArray(a_clan_id);

        delete doit;

        /** Pausa pred dalsim kolom na ulozenie databazy */
    //chrono::seconds dura(1); // pausa 3 sec
    //this_thread::sleep_for( dura );
    /*
        main2 = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed_main = main2 - main1;

        cout << elapsed_main.count() << " Sucasne kolo: " << i << " Zostava cids: " << cids.size() << endl;
    }
    
    t1 = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed_seconds = t1 - start;
    cout << "Pocet kol \t\t" << i << endl;
    cout << "Celkovy cas \t\t" << elapsed_seconds.count() << endl;

    cout << "Prichody do klanu \t" << stat.new_players_all << endl;
    cout << "Uplne nove ID \t\t" << stat.new_account_id << endl;
    cout << "Zmeny nickov \t\t" << stat.change_nick << endl;
    cout << "Ukoncene klany \t\t" << stat.empty_clan << endl;

    time(&stopprog);
    cout << "Program skoncil pracovat: " << ctime(&stopprog) << endl;

    PQfinish(permanent_connection);
    */
    return 0;
}
