class Spracuj
{

public:
    struct player
    {
        int account_id;
        int joined_at;
        string account_name;
        string role_i18n;
    };

    typedef map<int, player> mymap;

    mymap fresh, players_all, members_role;

    Spracuj(int clan_id[100], string json, PGconn *conn)
    {

        this->permanent_connection = conn;

        int i = 0, cid, members_count;

        int *p_members_count;
        p_members_count = &members_count;

        string clan_data;

        while (!clan_id[i] == 0)
        {
            cid = clan_id[i];

            // Natiahne do containeru players_all udaje z mojej databazy
            exec_select_players_all(cid);

            // Natiahne do containeru members_role udaje z mojej databazy
            exec_select_members_role(cid);

            // Vyberiem z json iba clanove udaje
            clan_data = this->spracuj_json_vyber_clan_id(json, cid, p_members_count);

            // Ak je clan prazdny, chcem to vediet a zapisat si to
            if (clan_data.empty())
            {
                // Zapis to do tabulky clan_all_empty
                this->clan_is_empty(cid);
                stat.empty_clan++;

                // Ak mam nejakych hracov v players_all zmen im clan_id na 0
                this->update_players_all_empty(cid);
                cout << "Klan " << cid << " je bez hracov " << endl;

                players_all.clear();
                fresh.clear();
                i++;
                continue;
            }

            // Ak je iny pocet clenov v tabulke clan_all oprav top
            if (members_count != (int)players_all.size())
            {
                this->update_clan_all_members_count(cid, members_count);
            }

            // Naplni container fresh udajmi z wargamingu
            this->spracuj_json_map(clan_data, p_members_count);

            this->spracuj_players_all(cid, players_all, fresh);
            this->spracuj_members_role(cid, members_role, fresh);

            i++;
            if (i >= 100)
            {
                break;
            }

            players_all.clear();
            fresh.clear();
            members_role.clear();
            clan_data.clear();
        }
    }

    ~Spracuj()
    {
    }

private:
    PGconn *permanent_connection;

    void spracuj_players_all(int clan_id, mymap players_all, mymap fresh)
    {
        map<int, player>::iterator it;
        map<int, player>::iterator it2;

        player pl;
        player fr;

        /* Hladam kto nie je v klane */
        for (it = players_all.begin(); it != players_all.end(); it++)
        {
            if (fresh.count(it->first) == 0)
            {
                this->nie_je_v_klane_players_all(it->first);
            }
        }

        /* Hladam kto je novy v klane */
        void novy_v_klane_players_all(int account_id, int clan_id, string account_name);

        for (it = fresh.begin(); it != fresh.end(); it++)
        {
            if (players_all.count(it->first) == 0)
            {
                fr = it->second;
                int aid = fr.account_id;
                string name = fr.account_name;

                this->novy_v_klane_players_all(aid, clan_id, name);
            }
        }

        /* Zmenil si nick ? */
        string name_fresh, name_players_all;

        for (it = fresh.begin(); it != fresh.end(); it++)
        {
            fr = it->second;

            it2 = players_all.find(fr.account_id);

            if (it2 != players_all.end())
            {
                pl = it2->second;

                name_fresh = fr.account_name;
                name_players_all = pl.account_name;

                if (name_fresh.compare(name_players_all) != 0)
                {
                    this->zmena_nick_players_all(name_players_all, name_fresh, fr.account_id);
                }

                name_fresh.clear();
                name_players_all.clear();
            }
        }
    }
    void spracuj_members_role(int clan_id, mymap members_role, mymap fresh)
    {
        map<int, player>::iterator it;
        map<int, player>::iterator it2;

        player mr;
        player fr;

        /* Hladam kto nie je v klane */
        for (it = members_role.begin(); it != members_role.end(); it++)
        {
            if (fresh.count(it->first) == 0)
            {
                this->nie_je_v_klane_members_role(it->first);
            }
        }

        /* Hladam kto je novy v klane */

        for (it = fresh.begin(); it != fresh.end(); it++)
        {
            if (members_role.count(it->first) == 0)
            {
                fr = it->second;
                int aid = fr.account_id;
                string role = fr.role_i18n;

                this->novy_v_klane_members_role(aid, clan_id, role);
            }
        }
    }

    string spracuj_json_vyber_clan_id(string json, int clan_id, int *members_count)
    {
        string clan_data;

        int begin, end;
        begin = json.find(to_string(clan_id));
        end = json.find("]},", begin);

        clan_data = json.substr(begin - 1, (end + 3) - begin);

        // Zistim kolko zaznamov ma klan
        begin = clan_data.find("members_count");
        end = clan_data.find(",", begin);
        try
        {
            *members_count = stoi(clan_data.substr(begin + 15, (end - begin + 15)));
        }
        catch (int e)
        {
            cout << "Chyba json vyber klan id" << endl;
            cout << clan_data << endl;
        }

        if (*members_count == 0)
        {
            clan_data.clear();
        }

        return clan_data;
    }

    void spracuj_json_map(string clan_data, int *members_count)
    {
        int begin, end, posled;
        player data;

        posled = 0;

        int id, join;
        id = join = 0;
        string role, nick;

        begin = clan_data.find("[");
        end = clan_data.find("]", begin);
        clan_data = clan_data.substr(begin, end - begin);

        while (begin > 0)
        {
            begin = clan_data.find("role_i18n", posled);
            if (begin == -1)
                break;
            end = clan_data.find(",", begin);
            role = clan_data.substr(begin + 12, (end - 1) - (begin + 12));

            begin = clan_data.find("joined_at", end);
            end = clan_data.find(",", begin);
            try
            {
                join = stoi(clan_data.substr(begin + 11, (end - 1) - (begin + 11)));
            }
            catch (int e)
            {
                cout << "JSON MAP " << endl;
            }

            begin = clan_data.find("account_name", end);
            end = clan_data.find(",", begin);
            nick = clan_data.substr(begin + 15, (end - 1) - (begin + 15));

            begin = clan_data.find("account_id", end);
            end = clan_data.find(",", begin);
            try
            {
                id = stoi(clan_data.substr(begin + 12, (end - 1) - (begin + 12)));
            }
            catch (int e)
            {
                cout << "JSON MAP 2" << endl;
            }

            posled = end;

            data.account_id = id;
            data.account_name = nick;
            data.joined_at = join;
            data.role_i18n = role;

            fresh[id] = data;
        }
    }

    /* ################## Praca s databazou ############################## */
    void novy_v_klane_members_role(int account_id, int clan_id, string role)
    {
        PGresult *result;
        int rows;
        /* Skontrolujem ci ma niekde nieco neukoncene */
        string q1 = "SELECT * FROM members_role WHERE account_id = " + to_string(clan_id) + " AND dokedy IS NULL";

        result = PQexec(this->permanent_connection, q1.c_str());
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            cout << "Chyba select members_role " << PQresultErrorMessage(result) << endl;
        }
        rows = PQntuples(result);
        PQclear(result);

        if (rows >= 1)
        {
            string q2 = "UPDATE members_role SET dokedy = now() WHERE dokedy IS NULL AND account_id = " + to_string(account_id);
            result = PQexec(this->permanent_connection, q2.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {
                cout << "Chyba update members_role stare dokedy " << PQresultErrorMessage(result) << endl;
            }
            PQclear(result);
        }

        /* Vlozim nove udaje do databazy */
        string q3 = "INSERT INTO members_role (account_id,clan_id,role,odkedy) VALUES (" + to_string(account_id) + "," + to_string(clan_id) + ",'" + role + "', now())";
        result = PQexec(this->permanent_connection, q3.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            cout << "Chyba insert members_role " << PQresultErrorMessage(result) << endl;
        }
        PQclear(result);
    }

    void nie_je_v_klane_members_role(int account_id)
    {
        PGresult *result;
        string query = "UPDATE members_role SET dokedy = now() WHERE dokedy IS NULL AND account_id = " + to_string(account_id);
        result = PQexec(this->permanent_connection, query.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            cout << "Chyba UPDATE now members_role " << PQresultErrorMessage(result) << endl;
        }
        PQclear(result);
        query.clear();
    }

    void zmena_nick_players_all(string old_name, string new_name, int account_id)
    {
        PGresult *result;
        string q1 = "INSERT INTO players_nick (account_id,nick) VALUES (" + to_string(account_id) + ",'" + old_name + "')";

        result = PQexec(this->permanent_connection, q1.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            cout << "Chyba INSERT players_nick " << PQresultErrorMessage(result) << endl;
        }
        PQclear(result);

        string q2 = "UPDATE players_all SET nickname = '" + new_name + "' WHERE account_id = " + to_string(account_id);
        result = PQexec(this->permanent_connection, q2.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            cout << "Chyba UPDATE nick in players_all " << PQresultErrorMessage(result) << endl;
        }
        PQclear(result);
        q1.clear();
        q2.clear();

        stat.change_nick++;
    }

    void nie_je_v_klane_players_all(int account_id)
    {
        PGresult *result;
        string query = "UPDATE players_all SET clan_id = 0 WHERE account_id = " + to_string(account_id);
        result = PQexec(this->permanent_connection, query.c_str());
        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            cout << "Chyba UPDATE 0 players_all " << PQresultErrorMessage(result) << endl;
        }
        PQclear(result);
        query.clear();
    }

    void novy_v_klane_players_all(int account_id, int clan_id, string account_name)
    {
        PGresult *result;
        int rows;

        string q1 = "SELECT account_id FROM players_all WHERE account_id = " + to_string(account_id);
        string i1 = "INSERT INTO players_all (account_id,clan_id,nickname) VALUES (" + to_string(account_id) + "," + to_string(clan_id) + ",'" + account_name + "' )";
        string u1 = "UPDATE players_all SET clan_id = " + to_string(clan_id) + " WHERE account_id = " + to_string(account_id);

        result = PQexec(this->permanent_connection, q1.c_str());

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            cout << "Chyba select players_all " << PQresultErrorMessage(result) << endl;
        }
        rows = PQntuples(result);

        PQclear(result);

        if (rows == 0)
        {
            result = PQexec(this->permanent_connection, i1.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {
                cout << "Chyba insert players_all " << PQresultErrorMessage(result) << endl;
            }

            PQclear(result);
            stat.new_account_id++;
        }
        else
        {
            result = PQexec(this->permanent_connection, u1.c_str());
            if (PQresultStatus(result) != PGRES_COMMAND_OK)
            {
                cout << "Chyba update players_all " << PQresultErrorMessage(result) << endl;
            }
            PQclear(result);
        }
        q1.clear();
        i1.clear();
        u1.clear();
        stat.new_players_all++;
    }

    void clan_is_empty(int cid)
    {
        string query = "INSERT INTO clan_all_empty VALUES (" + to_string(cid) + ") ";
        PQexec(this->permanent_connection, query.c_str());
    }
    void update_players_all_empty(int cid)
    {
        string query = "UPDATE players_all SET clan_id = 0 WHERE clan_id = " + to_string(cid);
        PQexec(this->permanent_connection, query.c_str());
    }
    void update_clan_all_members_count(int cid, int members_count)
    {
        string query = "UPDATE clan_all SET members_count = " + to_string(members_count) + " WHERE clan_id = " + to_string(cid);
        PQexec(this->permanent_connection, query.c_str());
    }

    void exec_select_players_all(int clan_id)
    {
        PGresult *result;
        const char *paramValues[1];
        int riadkov, i;

        string a_id = to_string(clan_id);
        paramValues[0] = a_id.c_str();

        player data;

        result = PQexecPrepared(this->permanent_connection, "players_all", 1, paramValues, NULL, NULL, 0);
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "Select statment players_all je chybny: %s ", PQresultErrorMessage(result));
        }

        riadkov = PQntuples(result);

        for (i = 0; i < riadkov; i++)
        {
            data.account_name = PQgetvalue(result, i, 1);
            data.account_id = stoi(PQgetvalue(result, i, 0));

            players_all[stoi(PQgetvalue(result, i, 0))] = data;
        }
        PQclear(result);
    }

    void exec_select_members_role(int clan_id)
    {
        PGresult *result;
        const char *paramValues[1];
        int riadkov, i;

        string a_id = to_string(clan_id);
        paramValues[0] = a_id.c_str();

        player data;

        result = PQexecPrepared(this->permanent_connection, "members_role", 1, paramValues, NULL, NULL, 0);
        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            fprintf(stderr, "Select statment members_role je chybny: %s ", PQresultErrorMessage(result));
        }

        riadkov = PQntuples(result);

        for (i = 0; i < riadkov; i++)
        {
            data.role_i18n = PQgetvalue(result, i, 1);
            data.account_id = stoi(PQgetvalue(result, i, 0));
            data.joined_at = stoi(PQgetvalue(result, i, 2));

            members_role[stoi(PQgetvalue(result, i, 0))] = data;
        }
        PQclear(result);
    }
};

void prepared_select_players_all(PGconn *conn)
{
    PGresult *result;

    const char *query = "SELECT account_id,nickname FROM players_all WHERE clan_id = $1 ";

    result = PQprepare(conn, "players_all", query, 1, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment players_all je chybny: %s", PQerrorMessage(conn));
        PQclear(result);
    }
    PQclear(result);
}

void prepared_select_members_role(PGconn *conn)
{
    PGresult *result;

    const char *query = "SELECT account_id,role,odkedy FROM members_role WHERE clan_id = $1 AND dokedy IS NULL";

    result = PQprepare(conn, "members_role", query, 1, NULL);
    if (PQresultStatus(result) != PGRES_COMMAND_OK)
    {
        fprintf(stderr, "Prepared statment members_role je chybny: %s", PQerrorMessage(conn));
        PQclear(result);
    }
    PQclear(result);
}