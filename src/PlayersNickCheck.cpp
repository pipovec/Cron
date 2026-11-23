/*
 * PlayersNickCheck - kontrola duplicitnych nickov v players_all (len CITA, nic nezapisuje)
 *
 * Najde nicky, ktore maju v players_all viac ako jeden account_id, a pre kazdy z tychto uctov
 * sa opyta WoT API (/account/info/) na aktualny nick. Vo WoT je nick jedinecny medzi zivymi
 * uctami, takze pri kazdej dvojici by mal ostat najviac jeden ucet, ktoremu API vrati ten isty
 * nick. Ostatne su zastarale zaznamy (premenovany alebo zmazany ucet).
 *
 * Verdikt pre ucet:
 *   OK         API vrati rovnaky nick ako players_all
 *   PREMENOVANY API vrati iny nick (ucet existuje, players_all ma stary nick)
 *   NEEXISTUJE API vrati null (ucet zmazany)
 *
 * Verdikt pre skupinu (nick):
 *   JEDNOZNACNE  presne jeden ucet je OK - ostatne sa daju opravit/oznacit
 *   ZIADEN_OK    ziaden ucet nema tento nick - zaznamy su vsetky zastarale
 *   KONFLIKT     viac uctov je OK (nemalo by nastat; API a DB sa mozu rozchadzat)
 *   CHYBA_API    pre niektory ucet sa nepodarilo ziskat odpoved
 *
 * Premenne prostredia:
 *   NICK_LIMIT   najviac N skupin (test), 0 = vsetky
 *   NICK_CSV     cesta k CSV so vsetkymi uctami (account_id;nick_db;nick_api;verdikt;skupina)
 *
 * Navratovy kod: 0 ok, 1 chyba DB alebo API.
 */

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "./lib/Pgsql.h"
#include "./lib/SendCurl.h"

using json = nlohmann::json;

struct Account
{
    long long id;
    std::string nick_db;
    std::string nick_api;
    bool fetched = false; // dostali sme odpoved pre tento ucet
    bool exists = false;
    std::string verdict;
};

static std::string Join(const std::vector<size_t> &idx, const std::vector<Account> &acc)
{
    std::string s;
    for (size_t k = 0; k < idx.size(); k++)
    {
        if (k)
            s += ',';
        s += std::to_string(acc[idx[k]].id);
    }
    return s;
}

int main()
{
    const long long limit = std::getenv("NICK_LIMIT") ? std::atoll(std::getenv("NICK_LIMIT")) : 0;

    Pgsql db;
    PGconn *conn = db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    std::string query =
        "SELECT account_id, nickname FROM players_all WHERE nickname IN ("
        "SELECT nickname FROM players_all WHERE nickname IS NOT NULL GROUP BY nickname HAVING count(*) > 1"
        + std::string(limit > 0 ? " ORDER BY nickname LIMIT " + std::to_string(limit) : "") +
        ") ORDER BY nickname, account_id";

    PGresult *result = PQexec(conn, query.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        std::cerr << "Chyba vyberu: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return 1;
    }

    std::vector<Account> acc;
    for (int i = 0; i < PQntuples(result); i++)
        acc.push_back({std::atoll(PQgetvalue(result, i, 0)), PQgetvalue(result, i, 1), "", false, false, ""});
    PQclear(result);

    std::cout << "Ucty s duplicitnym nickom: " << acc.size() << std::endl;

    // API po 100 uctoch
    SendCurl send;
    bool api_error = false;
    for (size_t begin = 0; begin < acc.size(); begin += 100)
    {
        size_t end = std::min(begin + 100, acc.size());
        std::string ids;
        for (size_t i = begin; i < end; i++)
            ids += (i > begin ? "," : "") + std::to_string(acc[i].id);

        std::string raw;
        try
        {
            raw = send.SendWOT("/account/info/", "&fields=nickname&account_id=" + ids);
        }
        catch (const std::string &e)
        {
            std::cerr << "API chyba: " << e << std::endl;
            api_error = true;
            continue;
        }
        catch (const std::exception &e)
        {
            std::cerr << "API chyba: " << e.what() << std::endl;
            api_error = true;
            continue;
        }

        json js = json::parse(raw, nullptr, false);
        if (js.is_discarded() || !js.is_object() || js.value("status", "") != "ok" || !js.contains("data") ||
            !js["data"].is_object())
        {
            std::cerr << "Neplatna odpoved API pre davku od " << acc[begin].id << std::endl;
            api_error = true;
            continue;
        }

        for (size_t i = begin; i < end; i++)
        {
            auto it = js["data"].find(std::to_string(acc[i].id));
            if (it == js["data"].end())
                continue; // bez odpovede - ostane fetched=false
            acc[i].fetched = true;
            if (it->is_object() && it->contains("nickname") && (*it)["nickname"].is_string())
            {
                acc[i].exists = true;
                acc[i].nick_api = (*it)["nickname"].get<std::string>();
            }
        }
    }

    for (Account &a : acc)
    {
        if (!a.fetched)
            a.verdict = "CHYBA_API";
        else if (!a.exists)
            a.verdict = "NEEXISTUJE";
        else if (a.nick_api == a.nick_db)
            a.verdict = "OK";
        else
            a.verdict = "PREMENOVANY";
    }

    // Skupiny podla nicku v DB (acc je zoradene podla nicku)
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < acc.size(); i++)
        groups[acc[i].nick_db].push_back(i);

    std::map<std::string, int> summary;
    std::ofstream csv;
    if (const char *path = std::getenv("NICK_CSV"); path != nullptr && *path != '\0')
    {
        csv.open(path);
        csv << "account_id;nick_db;nick_api;verdikt_uctu;verdikt_skupiny\n";
    }

    std::cout << "\nskupina | ucty | verdikt\n";
    for (const auto &[nick, idx] : groups)
    {
        int ok = 0, err = 0;
        for (size_t i : idx)
        {
            if (acc[i].verdict == "OK")
                ok++;
            else if (acc[i].verdict == "CHYBA_API")
                err++;
        }
        std::string gv = err > 0 ? "CHYBA_API" : ok == 1 ? "JEDNOZNACNE" : ok == 0 ? "ZIADEN_OK" : "KONFLIKT";
        summary[gv]++;

        std::cout << nick << " | " << Join(idx, acc) << " | " << gv << "\n";
        for (size_t i : idx)
        {
            const Account &a = acc[i];
            std::cout << "    " << a.id << "  " << a.verdict;
            if (a.verdict == "PREMENOVANY")
                std::cout << "  (teraz: " << a.nick_api << ")";
            std::cout << "\n";
            if (csv.is_open())
                csv << a.id << ";" << a.nick_db << ";" << a.nick_api << ";" << a.verdict << ";" << gv << "\n";
        }
    }

    std::cout << "\nSuhrn skupin (" << groups.size() << "):\n";
    for (const auto &[verdict, n] : summary)
        std::cout << "  " << verdict << ": " << n << "\n";
    std::cout << "Nic sa nezapisovalo do databazy." << std::endl;

    return api_error ? 1 : 0;
}
