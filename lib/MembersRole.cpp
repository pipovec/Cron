#include "MembersRole.h"


MembersRole::MembersRole()
{
    Pgsql *pg = new Pgsql;
    this->connection = pg->Get();
}

void MembersRole::spracuj(string json_string)
{
    using json = nlohmann::json;
    json raw_data, clans, clan_data;

    try{
        raw_data = json::parse(json_string);
    }    
    catch(json::parse_error& e) {
        cout << "Chyba v spracuj: " << e.what() << endl;
        chyba = true;
    }

    // Kontrola ci je status OK
    if (raw_data["status"].get<string>() == "ok" || chyba != true)
    {
        meta_count = raw_data["meta"]["count"].get<int>(); 
    }
    else
    {
        chyba = true;
    }
   
    clans = raw_data["data"];
    
    if (chyba == false)
    {
        for (auto& xxx : json::iterator_wrapper(clans))
        {
            clan_id = stoi(xxx.key());
            clan_data = xxx.value();

            this->parse_clan_data(clan_id, clan_data);            
        }   
    }
    
    
    chyba = false;
    clans.clear();
}

 void MembersRole::parse_clan_data(int clan_id, nlohmann::json clan_data)
 {
     string query = "SELECT account_id,role,odkedy FROM members_role WHERE dokedy IS NULL and clan_id="+to_string(clan_id);
     PGresult *result;
     
     result = PQexec(this->connection, query.c_str());
     if (PQresultStatus(result) != PGRES_TUPLES_OK)
                {cout << "SELECT FROM members_role BY clan_id FAIL! " <<  PQresultErrorMessage(result) << endl;}

    int riadkov     = PQntuples(result);
    cout << riadkov << endl;

     PQclear(result);

 }