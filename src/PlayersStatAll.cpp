/*
 * PlayersStatAll - Bojove statistiky pre vsetkych hracov v players_all
 *
 * Tenky wrapper okolo lib/PlayerStatProcessor - zdielana logika je popisana tam.
 * PLAYERSSTAT_LIMIT env premenna umoznuje obmedzit pocet spracovanych hracov
 * (napr. na testovanie).
 *
 * Author: Boris Fekar
 * Refactor: 2026-07-04
 */

#include "./lib/PlayerStatProcessor.h"

int main()
{
    PlayerStatProcessing::JobConfig cfg;
    cfg.cron_log_name = "cplusplus:PlayersStatAll";
    cfg.stat_type_name = "PlayersStatAll";
    cfg.count_query = "select count(account_id) from players_all";
    cfg.source_table = "players_all";
    cfg.run_cleanup = true;
    cfg.apply_env_limit = true;

    return PlayerStatProcessing::RunJob(cfg);
}
