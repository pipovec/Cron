#include "ClanData.h"

ClanData::ClanData() {}

ClanData::~ClanData() {}

void ClanData::EscapeName() {
    string ch = "'";
    size_t found = name.find(ch);

    if(found != string::npos) {
        name.insert(found, ch);

        size_t found1 = name.find(ch, found + 2);
        if(found1 != string::npos) {
            name.insert(found1, ch);
        }
    }
}

string ClanData::ToInsertValues() const {
    return "(" + clan_id + ",'" + tag + "'," + created_at + ",'" + name + "'," +
           members_count + ",'" + emblem195 + "','" + emblem64 + "','" +
           emblem32 + "','" + emblem24 + "')";
}

string ClanData::ToUpdateValues() const {
    return "(" + clan_id + ",'" + tag + "'," + created_at + ",'" + name + "'," +
           members_count + ",'" + emblem195 + "','" + emblem64 + "','" +
           emblem32 + "','" + emblem24 + "')";
}
