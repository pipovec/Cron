#include "./PlayerNick.h"

#include <iostream>

namespace PlayerNick
{

using namespace std;

// Jeden parametrizovany prikaz. Pri chybe vypise sprava a vrati false.
static bool Run(PGconn *conn, const char *sql, int n, const char *const *params, const char *what)
{
    PGresult *r = PQexecParams(conn, sql, n, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK || PQresultStatus(r) == PGRES_TUPLES_OK;
    if (!ok)
        cout << "Chyba " << what << ": " << PQresultErrorMessage(r) << endl;
    PQclear(r);
    return ok;
}

static void Rollback(PGconn *conn)
{
    PGresult *r = PQexec(conn, "ROLLBACK");
    PQclear(r);
}

// Spolocna cast: BEGIN + uvolnenie nicku u inych uctov. Na false uz je transakcia vratena.
static bool BeginAndRelease(PGconn *conn, int account_id, const string &nickname)
{
    if (!Run(conn, "BEGIN", 0, nullptr, "BEGIN"))
        return false;

    const string id = to_string(account_id);
    const char *p[2] = {nickname.c_str(), id.c_str()};

    // Zastarali drzitelia nicku: historia + nickname = NULL
    if (!Run(conn,
             "WITH freed AS (UPDATE players_all SET nickname = NULL "
             "WHERE nickname = $1 AND account_id <> $2::int RETURNING account_id) "
             "INSERT INTO players_nick (account_id, nick) SELECT account_id, $1 FROM freed",
             2, p, "uvolnenie nicku"))
    {
        Rollback(conn);
        return false;
    }
    return true;
}

bool Rename(PGconn *conn, int account_id, const string &old_name, const string &new_name)
{
    if (!BeginAndRelease(conn, account_id, new_name))
        return false;

    const string id = to_string(account_id);

    const char *h[2] = {id.c_str(), old_name.c_str()};
    const char *u[2] = {new_name.c_str(), id.c_str()};

    if (!Run(conn, "INSERT INTO players_nick (account_id, nick) VALUES ($1::int, $2)", 2, h, "INSERT players_nick") ||
        !Run(conn, "UPDATE players_all SET nickname = $1 WHERE account_id = $2::int", 2, u,
             "UPDATE nick in players_all"))
    {
        Rollback(conn);
        return false;
    }

    if (!Run(conn, "COMMIT", 0, nullptr, "COMMIT"))
    {
        Rollback(conn);
        return false;
    }
    return true;
}

bool InsertPlayer(PGconn *conn, int account_id, int clan_id, const string &nickname)
{
    if (!BeginAndRelease(conn, account_id, nickname))
        return false;

    const string id = to_string(account_id), clan = to_string(clan_id);
    const char *p[3] = {id.c_str(), clan.c_str(), nickname.c_str()};

    if (!Run(conn, "INSERT INTO players_all (account_id, clan_id, nickname) VALUES ($1::int, $2::int, $3)", 3, p,
             "insert players_all"))
    {
        Rollback(conn);
        return false;
    }

    if (!Run(conn, "COMMIT", 0, nullptr, "COMMIT"))
    {
        Rollback(conn);
        return false;
    }
    return true;
}

} // namespace PlayerNick
