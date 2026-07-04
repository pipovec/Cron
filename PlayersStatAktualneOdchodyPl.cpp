/*
 * PlayersStatAktualneOdchodyPl - Bojove statistiky pre PL hracov,
 * ktori aktualne odchadzaju z klanu (tabulka aktualne_odchody_pl)
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
    cfg.cron_log_name = "cplusplus:PlayersStatAktualneOdchodyPl";
    cfg.stat_type_name = "PlayersStatAktualneOdchodyPl";
    cfg.count_query = "SELECT count(account_id) FROM aktualne_odchody_pl";
    cfg.select_query = "SELECT account_id FROM aktualne_odchody_pl OFFSET ";
    cfg.run_cleanup = false;
    cfg.apply_env_limit = false;

    return PlayerStatProcessing::RunJob(cfg);
}
