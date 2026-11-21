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
