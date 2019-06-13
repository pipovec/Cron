/** 
*
*   Program skontroluje vsetky klany pomocou metody https://api.worldoftanks.eu/wgn/clans/list/
    Skontroluje vsetky udaje ci sa nezmenili a pripadne ich opravy
*
*   Autor: Boris Fekár alias pipovec alias BobyOneKiller
*
*/

#include <iostream>
#include <map>
#include <chrono>
#include <mutex>
#include <thread>

/** Kontainer **/
#include <map>

/**  Moje kniznice  */
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./json/src/json.hpp"

const string field = "&fields=clan_id,tag,created_at,name,members_count,emblems";
const string method   = "/clans/list/";
string json_string;


struct clan_data {
    string clan_id;
    string tag;
    string created_at;
    string name;
    string members_count;
    string emblem195;
    string emblem64;
    string emblem32;
    string emblem24;
};

/** Kontajner na ziskane udaje */
map<int,clan_data> jdata;

/** Spojenie do databazy */
Pgsql *p = new Pgsql;
PGconn *conn = p->pgsql;

int page = 1;
int total_page = 2;


/** Ziskanie dat zo servera */
void GetDataFromServer(int page)
{   
    int x = 1;
            
    chrono::seconds dura(3); // pausa 3 sec
    string post_data = field+"&page_no="+to_string(page);
    
    SendCurl send;
        do{
            try {
                json_string = send.SendWGN(method, post_data);
                x = 0;                
            }
            catch(exception& e) {
                cout << "Chyba v ziskani dat zo servera: " << e.what() << endl;
                this_thread::sleep_for( dura );
                x = 1;                
            }
        }
        while(x != 0);

    post_data.clear(); 
}

void TotalPage()
{
    using json = nlohmann::json;
    json js = json::parse(json_string);   

    int total = js["meta"]["total"].get<int>();

    total_page = (total / 100) + 1;
    js.clear();
}

void GetDataFromJson()
{
    using json = nlohmann::json;
    json js = json::parse(json_string); 
    json j;
    clan_data c;
    string ch = "'";
    
    /** Ziskanie clan_id */
    for (auto& x : json::iterator_wrapper(js["data"]))
    {
         j =  x.value() ;
         c.clan_id          = to_string(j["clan_id"].get<int>());
         c.tag              = j["tag"].get<string>();
         c.created_at       = to_string(j["created_at"].get<int>());
         
         c.name             = j["name"].get<string>();
          
         size_t found = c.name.find(ch);

         if(found != std::string::npos){
             c.name.insert(found,ch);

             size_t found1 = c.name.find(ch,found + 2);
             if(found1 != std::string::npos) {
             c.name.insert(found1,ch);
             }
         }
         
         c.members_count    = to_string(j["members_count"].get<int>());
         c.emblem195        = j["emblems"]["x195"]["portal"].get<string>();
         c.emblem64         = j["emblems"]["x64"]["portal"].get<string>();
         c.emblem32         = j["emblems"]["x32"]["portal"].get<string>();
         c.emblem24         = j["emblems"]["x24"]["portal"].get<string>();           
         
         jdata[stoi(c.clan_id)] = c;
            
    } 
   
    js.clear();j.clear();

}

void PreparedStatment()
{
    PGresult *result;
    const char *stmtName = "clan_all";
    const char *query  = "SELECT clan_id,abbreviation,name,members_count,emblems_tank,emblems_small,emblems_medium,emblems_large FROM clan_all WHERE clan_id = $1";
    
    result = PQprepare(conn,stmtName,query,1,NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment je chybny: %s", PQerrorMessage(conn));
        PQclear(result);
    }
    
}

bool Compare(PGresult *res, clan_data data)
{
    /** Abbreviation  */
    string comp = PQgetvalue(res,0,1);
    if(data.tag.compare(comp) != 0) {
        return true;
    }

    /** Name  */
    comp = PQgetvalue(res,0,2);
    if(data.name.compare(comp) != 0) {
        return true;
    }
    
    /** Members count  */
    comp = PQgetvalue(res,0,3);
    if(data.members_count.compare(comp) != 0) {
        return true;
    }

    /** Emblems tank 24  */
    comp = PQgetvalue(res,0,4);
    if(data.emblem24.compare(comp) != 0) {
        return true;
    }

    /** Emblems small 32  */
    comp = PQgetvalue(res,0,5);
    if(data.emblem32.compare(comp) != 0) {
        return true;
    }

    /** Emblems medium 64  */
    comp = PQgetvalue(res,0,6);
    if(data.emblem64.compare(comp) != 0) {
        return true;
    }

    /** Emblems medium 195  */
    comp = PQgetvalue(res,0,7);
    if(data.emblem195.compare(comp) != 0) {
        return true;
    }

    return false;
}

void InsertNewData(string data)
{
    //insert += "("+it->second.clan_id+",'"+it->second.tag+"',"+it->second.created_at+",'"+it->second.name+"',"+it->second.members_count+",'"+it->second.emblem195+"','"+it->second.emblem64+"','"+it->second.emblem32+"','"+it->second.emblem24+"')";
    string ins = "INSERT INTO clan_all (clan_id,abbreviation,created_at,name,members_count,emblems_large,emblems_medium,emblems_small,emblems_tank) VALUES ";
    
    if(data.size() > 0)
    {
        data.pop_back();
        PGresult *result;

        ins = ins + data ;
        
        result = PQexec(conn, ins.c_str());

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {cout << "Chyba insertu do tabulky clan_all "  <<  PQresultErrorMessage(result) << endl;}

        PQclear(result);data.clear();
    }
    
}

void UpdateData(string data)
{
    string values = data;
    if(data.size() > 0)
    {
        values.pop_back();
        PGresult *result;
                                                                                                                                                                                                            //update += "("+it->second.clan_id+",'"+it->second.tag+"',"+it->second.created_at+",'"+it->second.name+"',"+it->second.members_count+",'"+it->second.emblem195+"','"+it->second.emblem64+"','"+it->second.emblem32+"','"+it->second.emblem24+"'),";
        string query = "UPDATE clan_all as c SET abbreviation = u.tag, created_at = u.created_at, name = u.name, emblems_large = u.emblem195, emblems_medium = u.emblem64, emblems_small = u.emblem32, emblems_tank = u.emblem24 FROM (VALUES " + values + ") as u(clan_id,tag,created_at,name,members_count,emblem195,emblem64,emblem32,emblem24) WHERE c.clan_id = u.clan_id ";

        result = PQexec(conn, query.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {cout << "Chyba update tabulky clan_all" <<  PQresultErrorMessage(result) << endl;}

        PQclear(result);values.clear();
    }
}
void GetDataFromDatabese()
{
    
    PGresult *result;
    string insert;
    string update;
    clan_data dat;

    bool Compare(PGresult *res, clan_data dat);

    void PreparedStatment();

    void InsertNewData(string data);
    void UpdateData(string data);

    const char *stmtName = "clan_all";
    const char *paramValues[1];
    
    
    map<int,clan_data>::iterator it;
    
    for(it = jdata.begin(); it != jdata.end(); ++it)
    {
        paramValues[0] = to_string(it->first).c_str();
        
        result  = PQexecPrepared(conn,stmtName,1,paramValues,NULL,NULL,0);
        
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {fprintf(stderr, "Select statment je chybny: %s", PQresultErrorMessage(result));}        

        
        if(PQntuples(result) == 0) 
        {
            insert += "("+it->second.clan_id+",'"+it->second.tag+"',"+it->second.created_at+",'"+it->second.name+"',"+it->second.members_count+",'"+it->second.emblem195+"','"+it->second.emblem64+"','"+it->second.emblem32+"','"+it->second.emblem24+"'),";
            
        }
        
        /** Ak su udaje skontroluj ci ich treba updatnut */
        if(PQntuples(result) == 1)
        {
            
            dat = it->second;
            
            if(Compare(result,dat)) {
                update += "("+it->second.clan_id+",'"+it->second.tag+"',"+it->second.created_at+",'"+it->second.name+"',"+it->second.members_count+",'"+it->second.emblem195+"','"+it->second.emblem64+"','"+it->second.emblem32+"','"+it->second.emblem24+"'),";
            }
            
        }
        
    }
    
    
    InsertNewData(insert);insert.clear();
    UpdateData(update);
    PQclear(result);
    update.clear();
 
}
int main() {
    time_t start, stop;
    time(&start);

    cout << "*********************************"<< endl;
    cout << endl << "Program zacal pracovat: " << ctime(&start) << endl;
    
    /** Pripravim dotaz do databazy */
    PreparedStatment();


    while(page < total_page)
    {
        
        /** Ziskanie dat zo serveru */
        GetDataFromServer(page);
        if(page == 1) {
            TotalPage();
        }
        
        
        GetDataFromJson();
        
        GetDataFromDatabese();
        
        
        page ++;jdata.clear();json_string.clear();
       
    }
    time(&stop);
    cout << "Program skoncil pracovat: " << ctime(&stop) << endl;


    return 0;
}
