-- WN8 za 7/14/28 dni - krok 2: evidencia synchronizacie snapshotu
--
-- Spustenie:  psql -d clan -f sql/002_player_vehicle_sync.sql
--
-- player_vehicle_sync  pre kazdeho hraca datum posledneho riadka players_stat_all_history,
--                      ktory je uz zahrnuty v player_vehicle_snapshot ("snapshot je aktualny po
--                      tento datum"). Z neho denny job vie, koho spracovat (hraci s novsim riadkom
--                      v historii) a aky obdobie prirastku patri k jeho riadkom v historii.
--                      Zapisuje ho bootstrap (PlayersVehicleSnapshot) aj denny job (PlayersVehicleDelta).

CREATE TABLE IF NOT EXISTS player_vehicle_sync (
    account_id  integer     NOT NULL PRIMARY KEY REFERENCES players_all(account_id) ON UPDATE CASCADE ON DELETE CASCADE,
    synced_date date        NOT NULL,
    synced_at   timestamptz NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------------
-- Komentáre (idempotentné - súbor sa dá spustiť opakovane, komentáre sa prepíšu)
-- Zobrazenie: \d+ player_vehicle_sync
-- ---------------------------------------------------------------------------

COMMENT ON TABLE player_vehicle_sync IS
  'Evidencia synchronizácie snapshotu po hráčoch: dokedy z players_stat_all_history je zahrnuté v player_vehicle_snapshot. Podľa nej PlayersVehicleDelta zistí, ktorých hráčov a od ktorého dňa spracovať. Hráč bez riadka tu nemá východiskový stav, takže dostane len baseline (bez prírastkov).';
COMMENT ON COLUMN player_vehicle_sync.account_id  IS 'ID hráča z WoT (players_all.account_id).';
COMMENT ON COLUMN player_vehicle_sync.synced_date IS 'Dátum posledného riadka players_stat_all_history, ktorého bitky sú už zahrnuté v player_vehicle_snapshot.';
COMMENT ON COLUMN player_vehicle_sync.synced_at   IS 'Kedy bol hráč naposledy spracovaný (bootstrapom alebo denným jobom).';
