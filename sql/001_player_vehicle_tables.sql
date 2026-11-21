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
--
-- Konvencia ako inde v projekte: cudzi kluc na players_all (ON UPDATE/DELETE CASCADE),
-- timestamptz ako wn8player. updated_at sa nastavuje priamo v UPSERTe (nie triggerom) a len
-- ked sa stav zmenil - znamena "naposledy sa zmenil stav hraca na tanku".

CREATE TABLE IF NOT EXISTS player_vehicle_snapshot (
    account_id integer     NOT NULL REFERENCES players_all(account_id) ON UPDATE CASCADE ON DELETE CASCADE,
    tank_id    integer     NOT NULL,
    battles    integer     NOT NULL,
    wins       integer     NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (account_id, tank_id)
);

CREATE TABLE IF NOT EXISTS player_vehicle_daily (
    date       date        NOT NULL,
    account_id integer     NOT NULL REFERENCES players_all(account_id) ON UPDATE CASCADE ON DELETE CASCADE,
    tank_id    integer     NOT NULL,
    battles    integer     NOT NULL,
    wins       integer     NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (date, account_id, tank_id)
);

-- Index na referencujucom stlpci: bez neho by ON DELETE/UPDATE CASCADE z players_all robil
-- sekvencny scan cez cele player_vehicle_daily. Pouzitelny aj pre dotazy na jedneho hraca.
CREATE INDEX IF NOT EXISTS player_vehicle_daily_account_idx ON player_vehicle_daily (account_id, date);
