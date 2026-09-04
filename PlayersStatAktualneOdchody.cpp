/*
 * PlayersStatAktualneOdchody - Bojove statistiky pre CS/SK hracov,
 * ktori aktualne odchadzaju z klanu (tabulka aktualne_odchody)
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
    cfg.cron_log_name = "cplusplus:PlayersStatAktualneOdchody";
    cfg.stat_type_name = "PlayersStatAktualneOdchody";
    cfg.count_query = "SELECT count(account_id) FROM aktualne_odchody";
    cfg.source_table = "aktualne_odchody";
    cfg.run_cleanup = false;
    cfg.apply_env_limit = false;

    return PlayerStatProcessing::RunJob(cfg);
}
