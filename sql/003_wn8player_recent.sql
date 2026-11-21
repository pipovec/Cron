-- WN8 za 7/14/28 dní - krok 3: výsledná tabuľka
--
-- Spustenie:  psql -d clan -f sql/003_wn8player_recent.sql
-- Súbor sa dá spustiť opakovane (CREATE ... IF NOT EXISTS, komentáre sa prepíšu).
--
-- Plní ju PlayersWN8Recent (po PlayersVehicleDelta). Riadok existuje len pre hráča, ktorý v období
-- odohral aspoň WN8R_MIN_BATTLES bitiek a pokrytie prírastkov po tankoch dosiahlo aspoň
-- WN8R_MIN_COVERAGE %. Ak hráč podmienky v danom behu nespĺňa, jeho riadok sa zmaže -
-- chýbajúci riadok teda znamená "nedostatok dát", nie nulové WN8.

CREATE TABLE IF NOT EXISTS wn8player_recent (
    account_id integer       NOT NULL REFERENCES players_all(account_id) ON UPDATE CASCADE ON DELETE CASCADE,
    period     smallint      NOT NULL CHECK (period IN (7, 14, 28)),
    wn8        numeric(8,2)  NOT NULL,
    battles    integer       NOT NULL,
    coverage   numeric(6,2)  NOT NULL,
    date_from  date          NOT NULL,
    date_to    date          NOT NULL,
    updated_at timestamptz   NOT NULL DEFAULT now(),
    PRIMARY KEY (account_id, period)
);

-- Rebríčky: najlepší hráči za obdobie
CREATE INDEX IF NOT EXISTS wn8player_recent_period_wn8_idx ON wn8player_recent (period, wn8 DESC);

COMMENT ON TABLE wn8player_recent IS
  'WN8 hráča za posledných 7/14/28 dní (náhodné bitky). Skutočné hodnoty zo players_stat_all_history, očakávané hodnoty z player_vehicle_daily × expected_tank_value; vzorec je rovnaký ako v PlayersWN8v2 (lib/Wn8Formula). Plní PlayersWN8Recent. Chýbajúci riadok = málo bitiek alebo nízke pokrytie dát, nie WN8 = 0.';
COMMENT ON COLUMN wn8player_recent.account_id IS 'ID hráča z WoT (players_all.account_id).';
COMMENT ON COLUMN wn8player_recent.period     IS 'Dĺžka obdobia v dňoch: 7, 14 alebo 28.';
COMMENT ON COLUMN wn8player_recent.wn8        IS 'WN8 hráča za dané obdobie.';
COMMENT ON COLUMN wn8player_recent.battles    IS 'Počet náhodných bitiek hráča v období (súčet zo players_stat_all_history).';
COMMENT ON COLUMN wn8player_recent.coverage   IS 'Pokrytie v %: súčet prírastkov bitiek po tankoch (player_vehicle_daily) / bitky v histórii za rovnaké obdobie. Blízko 100 = spoľahlivá hodnota; nízke pokrytie znamená, že chýbal rozpad na tanky (napr. začiatok sledovania).';
COMMENT ON COLUMN wn8player_recent.date_from  IS 'Prvý deň obdobia (vrátane), dátum riadkov z players_stat_all_history.';
COMMENT ON COLUMN wn8player_recent.date_to    IS 'Posledný deň obdobia (vrátane) - najnovší deň, pre ktorý sú k dispozícii denné prírastky po tankoch.';
COMMENT ON COLUMN wn8player_recent.updated_at IS 'Kedy bol riadok naposledy prepočítaný (každý beh ho obnoví; staršie riadky sa na konci behu zmažú).';
COMMENT ON INDEX wn8player_recent_period_wn8_idx IS 'Rebríčky: hráči s najvyšším WN8 pre dané obdobie.';
