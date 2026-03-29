/*
*
*   Program prechadza vsetky klany a k nim vyberie vsetkych hracov v klane a pozrie ake maju
*   client_language. Podla toho rozhodne, akej narodnosti je ten klan a prida to do stlpca
*   v tabulke clan_all
*
*   Ak ma klan viac ako 5 jazykov je oznaceny ako medzinarodny
*/

#include <iostream>
#include <queue>
#include <map>

/**  Moje kniznice  */
#include "./lib/Pgsql.h"

using namespace std;

/** Kontajner na clan_id */
queue<string> clan_id;

/** Jedno spojenie do databazy */
PGconn *conn;

/** Kedy je oznaceny ako medzinarodny */
const int inter = 5;

/** Kam bude ukladata data na update */
string update;


/** Vytvor spojenie do databazy */
void CreateConnection()  {
    Pgsql *pg = new Pgsql;
    conn = pg->Get();
}

/** Nacitaj clany do fronty */
void GetClanId() {
    PGresult *result;
    const char *query = "SELECT clan_id FROM clan_all";

    result = PQexec(conn, query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {cout << "Ziskanie clan_id sa nepodarilo: " <<  PQresultErrorMessage(result) << endl;}

    int riadkov     = PQntuples(result);
    cout << "Pocet klanov na spracovanie:\t\t" << riadkov << endl;

    int i;
    for(i = 0; i < riadkov; i++)   {
        clan_id.push((PQgetvalue(result,i,0)) );
    }
    PQclear(result);
    cout << "Pocet natiahnutych klanov do queue:\t" << clan_id.size() << endl;
}

/** Vaccum a analyze tabuliek potrebnych pre program */
void VaccumAnalyze() {
    const char *players_info = "VACUUM ANALYZE players_info";
    const char *players_all = "VACUUM ANALYZE players_all";
    const char *clan_all = "VACUUM ANALYZE clan_all";

    PQsendQuery(conn, players_info);
    PQsendQuery(conn, players_all);
    PQsendQuery(conn, clan_all);

}

/** Priprav prepared statment */
void PrepareSTMT() {
    PGresult *result;
    const char *stmtName = "NarodneKlany";
    const char *query  = "SELECT client_language FROM players_all INNER JOIN players_info  ON players_info.account_id = players_all.account_id WHERE clan_id = $1";

    result = PQprepare(conn,stmtName,query,1,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)     {
        cout << "Prepared statment je chybny: " <<  PQerrorMessage(conn) << endl;
        PQclear(result);
    }

}

void Spracuj(string clan_id) {

    string GetNationale(PGresult *result, int i);

    PGresult *result;
    const char *stmtName = "NarodneKlany";
    const char *paramValues[1];
    string language = "empty";


    paramValues[0] = clan_id.c_str();

    result  = PQexecPrepared(conn,stmtName,1,paramValues,NULL,NULL,0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        cout << "Select statment je chybny: " << PQresultErrorMessage(result) << endl;
    }

    int i;
    i = PQntuples(result);

    /** Ak najde aspon jedneho hraca posli to na vyhodnotenie */
    if(i > 0) {
        language = GetNationale(result, i);
    }

    update += "("+clan_id+",'"+language+"'),";
    PQclear(result); language.clear();

}

/** Zisti ktory client_language v klane prevlada */
string GetNationale(PGresult *result, int i) {

    map<string,int> data;
    int c, lan;
    int pocet = 0;

    string res = "";

    for (c = 0; c < i; c++) {
        data[ PQgetvalue(result,c,0) ] ++;
    }

    lan = data.size();

    /** Ak je tam viac ako CONST INTER roznych jazykov, oznac ho ako medzinarodny  a */
    if(lan > inter) {
        return "inter";
    }
    else {
        /** Ktorych je najviac */
        for( map<string,int>::iterator it = data.begin(); it != data.end(); it++ ) {

            if(pocet < it->second) {
                res   = it->first;
                pocet = it->second;
            }
        }
    }

    data.clear();
    return res;
}

void UpdateData()
{
    update.pop_back();
    PGresult *result;
                                                                                                                                                                                                        //update += "("+it->second.clan_id+",'"+it->second.tag+"',"+it->second.created_at+",'"+it->second.name+"',"+it->second.members_count+",'"+it->second.emblem195+"','"+it->second.emblem64+"','"+it->second.emblem32+"','"+it->second.emblem24+"'),";
    string query = "UPDATE clan_all as c SET language = u.lang FROM (VALUES "+ update +")as u (clan_id,lang) WHERE c.clan_id = u.clan_id";
    
    result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {cout << "Chyba update tabulky clan_all" <<  PQresultErrorMessage(result) << endl;}

    PQclear(result);update.clear();

}

int main() {

    time_t start, stop;
    time(&start);

    cout << "*********************************"<< endl;
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;

    /** Vytvor spojenie */
    CreateConnection();

    /** Vycisti tabulky */
    VaccumAnalyze();

    /** Predpriprav dotaz */
    PrepareSTMT();

    /** Ziskaj clan_id do fronty */
    GetClanId();

    while(!clan_id.empty()) {
        Spracuj( clan_id.front() ); clan_id.pop();
    }

    /** Uloz spracovane data */
    UpdateData();

    /** Vycisti znova tabulky */
    VaccumAnalyze();

    time(&stop);
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;

    return 0;
}
