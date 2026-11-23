/*
 * PlayersNickCheck - kontrola duplicitnych nickov v players_all a mrtvych uctov
 *
 * Najde nicky, ktore maju v players_all viac ako jeden account_id (ucty uz zapisane v
 * players_dead sa preskakuju - ich zmaze delete_dead_players.sh), a pre kazdy z tychto uctov sa
 * opyta WoT API (/account/info/) na aktualny nick. Vo WoT je nick jedinecny medzi zivymi uctami,
 * takze pri kazdej dvojici by mal ostat najviac jeden ucet, ktoremu API vrati ten isty nick.
 *
 * Verdikt pre ucet:
 *   OK           API vrati rovnaky nick ako players_all
 *   PREMENOVANY  API vrati iny nick (ucet existuje, players_all ma stary nick)
 *   NEEXISTUJE   API vrati null (ucet je zmazany = mrtvy)
 *   CHYBA_API    pre ucet sa nepodarilo ziskat odpoved
 *
 * Verdikt pre skupinu (nick):
 *   JEDNOZNACNE  presne jeden ucet je OK
 *   ZIADEN_OK    ziaden ucet nema tento nick - vsetky zaznamy su zastarale
 *   KONFLIKT     viac uctov je OK (nemalo by nastat)
 *   CHYBA_API    pre niektory ucet chyba odpoved
 *
 * Predvolene len CITA a vypise report. S NICK_APPLY=1 zapise v JEDNEJ transakcii:
 *   PREMENOVANY  starý nick do players_nick, novy nick do players_all.nickname
 *                (len ak sa nick v players_all medzitym nezmenil a novy nick nema iny ucet,
 *                 inak sa ucet preskoci ako KOLIZIA)
 *   NEEXISTUJE   INSERT INTO players_dead (account_id, deleted_at) ON CONFLICT DO NOTHING;
 *                zo players_all ho zmaze existujuci delete_dead_players.sh
 *
 * Premenne prostredia:
 *   NICK_APPLY   1 = zapisat opravy (default: len report)
 *   NICK_LIMIT   najviac N skupin (test), 0 = vsetky
 *   NICK_CSV     cesta k CSV so vsetkymi uctami (account_id;nick_db;nick_api;verdikt_uctu;verdikt_skupiny)
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

static bool Exec(PGconn *conn, const char *sql, int n, const char *const *params, PGresult **out, ExecStatusType want)
{
    PGresult *r = PQexecParams(conn, sql, n, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) != want)
    {
        std::cerr << "SQL chyba: " << PQresultErrorMessage(r) << std::endl;
        PQclear(r);
        return false;
    }
    if (out)
        *out = r;
    else
        PQclear(r);
    return true;
}

struct ApplyStats
{
    int renamed = 0, collisions = 0, changed_meanwhile = 0, dead_inserted = 0, dead_already = 0;
};

// Zapise opravy v jednej transakcii. Pri chybe sa vsetko vrati.
static bool Apply(PGconn *conn, const std::vector<Account> &acc, ApplyStats *st)
{
    PGresult *r = PQexec(conn, "BEGIN");
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK;
    PQclear(r);
    if (!ok)
        return false;

    for (const Account &a : acc)
    {
        const std::string id = std::to_string(a.id);

        if (a.verdict == "PREMENOVANY")
        {
            // Novy nick nesmie uz drzat iny ucet (zastaraly zaznam nechavame na dalsi beh / rucne)
            const char *p1[2] = {a.nick_api.c_str(), id.c_str()};
            PGresult *c = nullptr;
            if (!Exec(conn, "SELECT 1 FROM players_all WHERE nickname = $1 AND account_id <> $2 LIMIT 1", 2, p1, &c,
                      PGRES_TUPLES_OK))
                ok = false;
            else
            {
                bool collision = PQntuples(c) > 0;
                PQclear(c);
                if (collision)
                {
                    st->collisions++;
                    std::cout << "KOLIZIA " << id << ": nick '" << a.nick_api << "' uz drzi iny ucet, preskakujem"
                              << std::endl;
                    continue;
                }
            }
            if (!ok)
                break;

            // Najprv UPDATE s poistkou (nick sa medzitym nezmenil), historia len ak sa zmenil
            const char *p2[3] = {a.nick_api.c_str(), id.c_str(), a.nick_db.c_str()};
            PGresult *u = nullptr;
            if (!Exec(conn,
                      "UPDATE players_all SET nickname = $1 WHERE account_id = $2 AND nickname = $3 RETURNING 1", 3,
                      p2, &u, PGRES_TUPLES_OK))
            {
                ok = false;
                break;
            }
            bool changed = PQntuples(u) > 0;
            PQclear(u);
            if (!changed)
            {
                st->changed_meanwhile++;
                continue;
            }

            const char *p3[2] = {id.c_str(), a.nick_db.c_str()};
            if (!Exec(conn, "INSERT INTO players_nick (account_id, nick) VALUES ($1, $2)", 2, p3, nullptr,
                      PGRES_COMMAND_OK))
            {
                ok = false;
                break;
            }
            st->renamed++;
        }
        else if (a.verdict == "NEEXISTUJE")
        {
            const char *p[1] = {id.c_str()};
            PGresult *d = nullptr;
            if (!Exec(conn,
                      "INSERT INTO players_dead (account_id, deleted_at) VALUES ($1, now()) "
                      "ON CONFLICT (account_id) DO NOTHING",
                      1, p, &d, PGRES_COMMAND_OK))
            {
                ok = false;
                break;
            }
            if (std::atoi(PQcmdTuples(d)) > 0)
                st->dead_inserted++;
            else
                st->dead_already++;
            PQclear(d);
        }
    }

    r = PQexec(conn, ok ? "COMMIT" : "ROLLBACK");
    PQclear(r);
    return ok;
}

int main()
{
    const long long limit = std::getenv("NICK_LIMIT") ? std::atoll(std::getenv("NICK_LIMIT")) : 0;
    const bool apply = std::getenv("NICK_APPLY") && std::string(std::getenv("NICK_APPLY")) == "1";

    Pgsql db;
    PGconn *conn = db.Get();
    if (conn == nullptr)
    {
        std::cerr << "Bez spojenia s databazou" << std::endl;
        return 1;
    }

    // Duplicity medzi uctami, ktore este nie su v players_dead
    const std::string live = "FROM players_all p WHERE p.nickname IS NOT NULL AND NOT EXISTS "
                             "(SELECT 1 FROM players_dead d WHERE d.account_id = p.account_id)";
    std::string query =
        "SELECT account_id, nickname FROM players_all WHERE nickname IN ("
        "SELECT p.nickname " + live + " GROUP BY p.nickname HAVING count(*) > 1" +
        (limit > 0 ? " ORDER BY p.nickname LIMIT " + std::to_string(limit) : std::string()) +
        ") AND NOT EXISTS (SELECT 1 FROM players_dead d WHERE d.account_id = players_all.account_id) "
        "ORDER BY nickname, account_id";

    PGresult *result = nullptr;
    if (!Exec(conn, query.c_str(), 0, nullptr, &result, PGRES_TUPLES_OK))
        return 1;

    std::vector<Account> acc;
    for (int i = 0; i < PQntuples(result); i++)
        acc.push_back({std::atoll(PQgetvalue(result, i, 0)), PQgetvalue(result, i, 1), "", false, false, ""});
    PQclear(result);

    std::cout << "Ucty s duplicitnym nickom: " << acc.size() << (apply ? " (rezim APPLY)" : " (len report)")
              << std::endl;

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

    std::map<std::string, int> summary, accounts_summary;
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
            accounts_summary[a.verdict]++;
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
    std::cout << "Suhrn uctov (" << acc.size() << "):\n";
    for (const auto &[verdict, n] : accounts_summary)
        std::cout << "  " << verdict << ": " << n << "\n";

    int rc = api_error ? 1 : 0;
    if (!apply)
    {
        std::cout << "Rezim len report: nic sa nezapisovalo (zapis: NICK_APPLY=1)." << std::endl;
        return rc;
    }

    ApplyStats st;
    if (!Apply(conn, acc, &st))
    {
        std::cerr << "Zapis zlyhal, transakcia vratena, nic sa nezmenilo." << std::endl;
        return 1;
    }
    std::cout << "Zapisane: premenovanych nickov " << st.renamed << ", novych v players_dead " << st.dead_inserted
              << " (uz tam boli " << st.dead_already << "), kolizii " << st.collisions
              << ", zmenene medzitym " << st.changed_meanwhile << std::endl;
    return rc;
}
