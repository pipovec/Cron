#include "Calculate.h"

Calculate::Calculate()
{
    this->loadEtvTable();
}

void Calculate::set(queue<std::string> jsons)
{
    while(jsons.size() > 0)
    {
        this->jsonString = jsons.front();
        jsons.pop();
        this->playerStatistic();
    }
}

void Calculate::playerStatistic()
{
    using json = nlohmann::json;
    json js;

    int battles = 0;
    int damage_dealt = 0;
    int dropped_capture_points = 0;
    int frags = 0;
    int spotted = 0;
    int wins = 0;
    int mark_of_mastery = 0;

    js = json::parse(this->jsonString);

    for (auto& [key, val] : js["data"].items())
    {
        for (auto& [k,v] : val.items())
        {
            battles += v["all"]["battles"].get<int>();
            damage_dealt += v["all"]["damage_dealt"].get<int>();
            dropped_capture_points += v["all"]["dropped_capture_points"].get<int>();
            frags += v["all"]["frags"].get<int>();
            spotted += v["all"]["spotted"].get<int>();
            wins += v["all"]["wins"].get<int>();
        }

        // Hracove statistiky celkom
        this->player_stat.account_id = stoi(key);
        this->player_stat.battles = battles;
        this->player_stat.damage_dealt = damage_dealt;
        this->player_stat.dropped_capture_points = dropped_capture_points;
        this->player_stat.frags = frags;
        this->player_stat.spotted = spotted;
        this->player_stat.wins = wins;

        cout << "Hracove statistiky: " << this->player_stat.account_id << " - " << this->player_stat.battles << endl;
    }
}

void Calculate::loadEtvTable()
{
    PGresult *result;
    PGconn *conn;
    Pgsql *pgsql    = new Pgsql;
    conn = pgsql->Get();

    const char query[] = "SELECT * FROM expected_tank_value"; 

    result = PQexec(conn,query);
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
            {cout << "Chyba v select etv table: " <<  PQresultErrorMessage(result) << endl;}

    int ntuples = PQntuples(result);int tank_id;int i;
    for(i = 0; i < ntuples; i++)
    {
        tank_id    = stoi(PQgetvalue(result,i,0));

        data.frag  = stof(PQgetvalue(result,i,1));
        data.dmg   = stof(PQgetvalue(result,i,2));
        data.spot  = stof(PQgetvalue(result,i,3));
        data.def   = stof(PQgetvalue(result,i,4));
        data.win   = stof(PQgetvalue(result,i,5));

        this->etv[tank_id] = data;

    }
    cout << "etv tabulka nacitana. pocet riadkov: " << ntuples << endl;
    PQclear(result);
    PQfinish(conn);
}

Calculate::~Calculate()
{}
