#include "ClanData.h"

ClanData::ClanData() {}

ClanData::~ClanData() {}

void ClanData::EscapeName() {
    // Escape VSETKY apostrofy v name, tag a ostatnych poliach
    name = EscapeString(name);
    tag = EscapeString(tag);
}

string ClanData::EscapeString(const string& str) {
    string result;
    result.reserve(str.size() + 10); // Reserve extra space

    for(char c : str) {
        if(c == '\'') {
            result += "''";  // SQL escape: ' -> ''
        } else {
            result += c;
        }
    }

    return result;
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
