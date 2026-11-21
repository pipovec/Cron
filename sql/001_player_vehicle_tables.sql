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

-- ---------------------------------------------------------------------------
-- Komentáre (idempotentné - súbor sa dá spustiť opakovane, komentáre sa prepíšu)
-- Zobrazenie: \d+ player_vehicle_snapshot
-- ---------------------------------------------------------------------------

COMMENT ON TABLE player_vehicle_snapshot IS
  'Posledný známy stav bitiek a výhier hráča na každom tanku (zdroj: WoT API /account/tanks/). Slúži len ako východiskový stav na výpočet denných prírastkov (player_vehicle_daily). Plní ju bootstrap PlayersVehicleSnapshot a denný job PlayersVehicleDelta.';
COMMENT ON COLUMN player_vehicle_snapshot.account_id IS 'ID hráča z WoT (players_all.account_id).';
COMMENT ON COLUMN player_vehicle_snapshot.tank_id    IS 'ID vozidla z WoT (encyclopedia_vehicles.tank_id).';
COMMENT ON COLUMN player_vehicle_snapshot.battles    IS 'Celkový počet náhodných bitiek hráča na tomto tanku v čase posledného stiahnutia (kumulatívne, nie prírastok).';
COMMENT ON COLUMN player_vehicle_snapshot.wins       IS 'Celkový počet víťazstiev hráča na tomto tanku v čase posledného stiahnutia (kumulatívne).';
COMMENT ON COLUMN player_vehicle_snapshot.created_at IS 'Kedy bol riadok vložený, teda kedy sa tank prvýkrát objavil v snapshote hráča.';
COMMENT ON COLUMN player_vehicle_snapshot.updated_at IS 'Kedy sa naposledy zmenili battles alebo wins. UPSERT ho nastaví len pri skutočnej zmene, nie pri každom stiahnutí.';

COMMENT ON TABLE player_vehicle_daily IS
  'Denné prírastky bitiek a výhier hráča po tankoch. Zdroj menovateľa WN8 za 7/14/28 dní (súčet očakávaných hodnôt tanku × prírastok bitiek). Plní PlayersVehicleDelta, riadky staršie ako 35 dní sa mažú.';
COMMENT ON COLUMN player_vehicle_daily.date       IS 'Dátum riadka z players_stat_all_history, ktorý prírastok pokrýva (najnovší spracovaný dátum; po výpadku jobu môže zahŕňať viac dní).';
COMMENT ON COLUMN player_vehicle_daily.account_id IS 'ID hráča z WoT (players_all.account_id).';
COMMENT ON COLUMN player_vehicle_daily.tank_id    IS 'ID vozidla z WoT (encyclopedia_vehicles.tank_id).';
COMMENT ON COLUMN player_vehicle_daily.battles    IS 'PRÍRASTOK náhodných bitiek na tanku za dané obdobie (nie kumulatívny súčet).';
COMMENT ON COLUMN player_vehicle_daily.wins       IS 'PRÍRASTOK víťazstiev na tanku za dané obdobie (nie kumulatívny súčet).';
COMMENT ON COLUMN player_vehicle_daily.created_at IS 'Kedy job tento riadok zapísal.';

COMMENT ON INDEX player_vehicle_daily_account_idx IS
  'Index na referencujúcom stĺpci: bez neho by ON DELETE CASCADE z players_all skenoval celú tabuľku. Využiteľný aj pre dotazy na jedného hráča.';
