/*
*   Trieda pre ukladanie dat o klane
*
*   author: Boris Fekar
*   date: 2025-11-15
*/

#ifndef CLANDATA_H
#define CLANDATA_H

#include <string>

using namespace std;

class ClanData {
public:
    string clan_id;
    string tag;
    string created_at;
    string name;
    string members_count;
    string emblem195;
    string emblem64;
    string emblem32;
    string emblem24;

    ClanData();
    ~ClanData();

    // Escape apostrofy v mene a tagu pre SQL
    void EscapeName();

    // Vygeneruje INSERT VALUES string pre tento klan
    string ToInsertValues() const;

    // Vygeneruje UPDATE VALUES string pre tento klan
    string ToUpdateValues() const;

private:
    // Pomocna metoda - escape vsetkych apostrofov v stringu
    static string EscapeString(const string& str);
};

#endif
