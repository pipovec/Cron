/*
 * PlayersStat - Bojove statistiky pre hracov z nabor_new
 * Binarka: PlayersStatNaborNew
 *
 * Tenky wrapper okolo lib/PlayerStatProcessor - zdielana logika je popisana tam.
 *
 * Author: Boris Fekar
 * Refactor: 2026-07-04
 */

#include "./lib/PlayerStatProcessor.h"

int main()
{
    PlayerStatProcessing::JobConfig cfg;
    cfg.cron_log_name = "cplusplus:PlayersStatNaborNew";
    cfg.stat_type_name = "PlayersStatNaborNew";
    cfg.count_query = "select count(account_id) from nabor_new";
    cfg.source_table = "nabor_new";
    cfg.run_cleanup = true;
    cfg.apply_env_limit = false;

    return PlayerStatProcessing::RunJob(cfg);
}
