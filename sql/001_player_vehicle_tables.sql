-- WN8 za 7/14/28 dni - krok 1: tabulky
--
-- Spustenie:  psql -d clan -f sql/001_player_vehicle_tables.sql
--
-- player_vehicle_snapshot  posledny znamy stav bitiek/vyhier hraca na kazdom tanku
--                          (zdroj /account/tanks/). Sluzi na vypocet dennych prirastkov.
-- player_vehicle_daily     denne prirastky po tankoch (len tanky, na ktorych hrac ten den hral).
--                          Vypocet WN8 za obdobie: suma exp(tank) * prirastok bitiek za N dni.
--
-- Starsiu tabulku player_vehicle_stat (130 M riadkov, uz neaktualizovana) tento skript nemeni.
-- Bez cudzieho kluca na players_all zamerne: hromadny zapis ~130 M riadkov by spomalil.

CREATE TABLE IF NOT EXISTS player_vehicle_snapshot (
    account_id integer NOT NULL,
    tank_id    integer NOT NULL,
    battles    integer NOT NULL,
    wins       integer NOT NULL,
    PRIMARY KEY (account_id, tank_id)
);

CREATE TABLE IF NOT EXISTS player_vehicle_daily (
    date       date    NOT NULL,
    account_id integer NOT NULL,
    tank_id    integer NOT NULL,
    battles    integer NOT NULL,
    wins       integer NOT NULL,
    PRIMARY KEY (date, account_id, tank_id)
);
