#include "rapidjson/document.h"
#include <iostream>
#include <fstream>

using namespace rapidjson;
using namespace std;

int main() {

    string json;

    ifstream file("json/json0.json");

    file >> json ;

    file.close();

    Document doc;
    doc.Parse(json.c_str());

    unsigned int i;
    string si;
    cout << doc["status"].GetString() << endl;

    Value &data = doc["data"];
    
    
    for (Value::ConstMemberIterator itr = data.MemberBegin();itr != data.MemberEnd(); ++itr)
    {
         si = itr->name.GetString() ;
         i = stoi(si);



         if(data[itr->name.GetString()].IsObject())
           {
               cout << itr->name.GetString() << " " << 
               data[itr->name.GetString()]["client_language"].GetString() << " " <<
               data[itr->name.GetString()]["created_at"].GetInt() << " " <<
               data[itr->name.GetString()]["last_battle_time"].GetInt() << " " << endl;
           }
    }



    return 0;
}