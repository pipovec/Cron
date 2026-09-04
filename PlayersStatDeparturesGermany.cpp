/*
 * PlayersStatDeparturesGermany - Bojove statistiky pre DE hracov,
 * ktori aktualne odchadzaju z klanu (tabulka departures_germany)
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
    cfg.cron_log_name = "cplusplus:PlayersStatDeparturesGermany";
    cfg.stat_type_name = "PlayersStatDeparturesGermany";
    cfg.count_query = "SELECT count(account_id) FROM departures_germany";
    cfg.source_table = "departures_germany";
    cfg.run_cleanup = false;
    cfg.apply_env_limit = false;

    return PlayerStatProcessing::RunJob(cfg);
}
