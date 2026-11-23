-- player_vehicle_stat: rozdelenie poslednej (otvorenej) particie _15 na 10 particii
--
-- Pred: _15 = <543 952 910 ; 999 999 999) s ~791 000 hracmi (ostatne particie maju 67-85 tisic).
-- Po:   _15 az _24, kazda ~79 135 hracov; posledna (_24) ostava otvorena pre novych hracov.
--
-- Hranice su vypocitane z players_stat_all (ntile(10) nad account_id >= 543 952 910, stav
-- 2026-11-23). Pred spustenim ich mozes prepocitat dotazom z casti 0.
--
-- Spustenie:  psql -d clan -v ON_ERROR_STOP=1 -f sql/004_player_vehicle_stat_repartition.sql
--
-- POZOR:
--   * DETACH PARTITION vezme ACCESS EXCLUSIVE zamok na player_vehicle_stat. Cela operacia je
--     jedna transakcia, kym sa presuva ~20+ M riadkov (rad minut), zapisy aj citania tabulky cakaju.
--     Spusti ju, ked nebezi PlayersVehicleStat (cron 08:00) ani iny program, co tabulku cita
--     (PlayersStatNaborNew 6/12/18/22 h, admin nabor, refresh mv_player_vehicle_summary kazdych 20 min).
--   * Skript NEMAZE povodne data. Stara particia ostane ako player_vehicle_stat_15_old; zmaz ju az
--     po kontrole (cast 3).
--   * Skript nie je idempotentny: druhy beh zlyha na DETACH/rename. To je zamer.
--   * Potrebuje miesto na disku pre kopiu particie (~20 M riadkov + indexy, par GB).

-- 0) Kontrola hranic (nic nemeni). Vystup ma mat 10 riadkov s ~79 tisic hracmi.
--    SELECT b, min(account_id) AS lo, max(account_id) AS hi, count(*) AS players
--    FROM (SELECT account_id, ntile(10) OVER (ORDER BY account_id) AS b
--          FROM players_stat_all WHERE account_id >= 543952910) x
--    GROUP BY b ORDER BY b;

-- 1) Predpoklady
DO $$
BEGIN
    IF to_regclass('public.player_vehicle_stat_15') IS NULL THEN
        RAISE EXCEPTION 'player_vehicle_stat_15 neexistuje';
    END IF;
    IF to_regclass('public.player_vehicle_stat_15_old') IS NOT NULL THEN
        RAISE EXCEPTION 'player_vehicle_stat_15_old uz existuje - skript uz bezal';
    END IF;
    IF (SELECT pg_get_expr(relpartbound, oid) FROM pg_class WHERE oid = 'public.player_vehicle_stat_15'::regclass)
       <> 'FOR VALUES FROM (543952910) TO (999999999)' THEN
        RAISE EXCEPTION 'player_vehicle_stat_15 nema ocakavane hranice';
    END IF;
END $$;

-- 2) Rozdelenie v jednej transakcii
BEGIN;

SET LOCAL lock_timeout = '60s';          -- ak sa zamok nedostane (bezi cron), radsej zlyhaj
SET LOCAL maintenance_work_mem = '1GB';  -- rychlejsie budovanie indexov

ALTER TABLE player_vehicle_stat DETACH PARTITION player_vehicle_stat_15;
ALTER TABLE player_vehicle_stat_15 RENAME TO player_vehicle_stat_15_old;
ALTER INDEX player_vehicle_stat_15_pkey RENAME TO player_vehicle_stat_15_old_pkey;
ALTER INDEX idx_player_vehicle_stat_15_tank_id RENAME TO idx_player_vehicle_stat_15_old_tank_id;

CREATE TABLE player_vehicle_stat_15 PARTITION OF player_vehicle_stat FOR VALUES FROM (543952910) TO (550983337);
CREATE TABLE player_vehicle_stat_16 PARTITION OF player_vehicle_stat FOR VALUES FROM (550983337) TO (558978028);
CREATE TABLE player_vehicle_stat_17 PARTITION OF player_vehicle_stat FOR VALUES FROM (558978028) TO (567913594);
CREATE TABLE player_vehicle_stat_18 PARTITION OF player_vehicle_stat FOR VALUES FROM (567913594) TO (581437250);
CREATE TABLE player_vehicle_stat_19 PARTITION OF player_vehicle_stat FOR VALUES FROM (581437250) TO (594937679);
CREATE TABLE player_vehicle_stat_20 PARTITION OF player_vehicle_stat FOR VALUES FROM (594937679) TO (596493106);
CREATE TABLE player_vehicle_stat_21 PARTITION OF player_vehicle_stat FOR VALUES FROM (596493106) TO (602079696);
CREATE TABLE player_vehicle_stat_22 PARTITION OF player_vehicle_stat FOR VALUES FROM (602079696) TO (666392833);
CREATE TABLE player_vehicle_stat_23 PARTITION OF player_vehicle_stat FOR VALUES FROM (666392833) TO (701272002);
CREATE TABLE player_vehicle_stat_24 PARTITION OF player_vehicle_stat FOR VALUES FROM (701272002) TO (999999999);

-- Rovnake nastavenie autovacuum ako ostatne particie
ALTER TABLE player_vehicle_stat_15 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_16 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_17 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_18 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_19 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_20 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_21 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_22 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_23 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);
ALTER TABLE player_vehicle_stat_24 SET (autovacuum_vacuum_scale_factor = 0.01, autovacuum_analyze_scale_factor = 0.005);

-- Presun dat: riadky sa samy rozdelia podla account_id do novych particii
INSERT INTO player_vehicle_stat (account_id, tank_id, battles, wins, mark_of_mastery)
SELECT account_id, tank_id, battles, wins, mark_of_mastery FROM player_vehicle_stat_15_old;

-- Lokalny index na tank_id (ako maju ostatne particie; na rodicovi nie je)
CREATE INDEX idx_player_vehicle_stat_15_tank_id ON player_vehicle_stat_15 (tank_id);
CREATE INDEX idx_player_vehicle_stat_16_tank_id ON player_vehicle_stat_16 (tank_id);
CREATE INDEX idx_player_vehicle_stat_17_tank_id ON player_vehicle_stat_17 (tank_id);
CREATE INDEX idx_player_vehicle_stat_18_tank_id ON player_vehicle_stat_18 (tank_id);
CREATE INDEX idx_player_vehicle_stat_19_tank_id ON player_vehicle_stat_19 (tank_id);
CREATE INDEX idx_player_vehicle_stat_20_tank_id ON player_vehicle_stat_20 (tank_id);
CREATE INDEX idx_player_vehicle_stat_21_tank_id ON player_vehicle_stat_21 (tank_id);
CREATE INDEX idx_player_vehicle_stat_22_tank_id ON player_vehicle_stat_22 (tank_id);
CREATE INDEX idx_player_vehicle_stat_23_tank_id ON player_vehicle_stat_23 (tank_id);
CREATE INDEX idx_player_vehicle_stat_24_tank_id ON player_vehicle_stat_24 (tank_id);

-- Poistka: pocet riadkov sa musi zhodovat, inak sa transakcia vrati
DO $$
DECLARE
    old_rows bigint;
    new_rows bigint;
BEGIN
    SELECT count(*) INTO old_rows FROM player_vehicle_stat_15_old;
    SELECT count(*) INTO new_rows FROM player_vehicle_stat WHERE account_id >= 543952910;
    IF old_rows <> new_rows THEN
        RAISE EXCEPTION 'Nezhoda poctu riadkov: stara particia %, nove %', old_rows, new_rows;
    END IF;
    RAISE NOTICE 'OK: presunutych % riadkov', new_rows;
END $$;

COMMIT;

-- Statistiky pre planovac (mimo transakcie)
ANALYZE player_vehicle_stat_15;
ANALYZE player_vehicle_stat_16;
ANALYZE player_vehicle_stat_17;
ANALYZE player_vehicle_stat_18;
ANALYZE player_vehicle_stat_19;
ANALYZE player_vehicle_stat_20;
ANALYZE player_vehicle_stat_21;
ANALYZE player_vehicle_stat_22;
ANALYZE player_vehicle_stat_23;
ANALYZE player_vehicle_stat_24;

-- 3) Kontrola a upratanie (spusti rucne, ked si overil vysledok):
--    SELECT c.relname, pg_get_expr(c.relpartbound, c.oid) AS bound, s.n_live_tup
--    FROM pg_inherits i JOIN pg_class c ON c.oid = i.inhrelid
--    LEFT JOIN pg_stat_user_tables s ON s.relid = c.oid
--    WHERE i.inhparent = 'player_vehicle_stat'::regclass ORDER BY c.relname;
--
--    DROP TABLE player_vehicle_stat_15_old;
