/*
 * PlayerStatProcessor - Zjednotena logika pre PlayersStat/PlayersStatAll/
 * PlayersStatAktualneOdchody/PlayersStatAktualneOdchodyPl/PlayersStatDeparturesGermany
 *
 * Vsetky tieto programy robia to iste - zoberu account_id z nejakej zdrojovej
 * tabulky, dotiahnu bojove statistiky z WoT API a upsertnu ich do
 * players_stat_all/defense/skirmish/gm_absolute/gm_champion (+ _history).
 * Lisia sa len zdrojovou tabulkou account_id a nazvom pre cron_log.
 *
 * Author: Boris Fekar
 * Refactor: 2026-07-04
 */

#ifndef PLAYERSTATPROCESSOR_H
#define PLAYERSTATPROCESSOR_H

#include <string>

namespace PlayerStatProcessing
{

struct JobConfig
{
    std::string cron_log_name;   // napr. "cplusplus:PlayersStatAll"
    std::string stat_type_name;  // "Typ=" hodnota v cron_log info
    std::string count_query;     // "SELECT count(account_id) FROM players_all"
    std::string source_table;    // zdrojova tabulka s account_id, napr. "players_all"
                                 // (strankuje sa keysetom podla account_id)
    bool run_cleanup = false;    // mazanie *_history starsich ako 2 mesiace
    bool apply_env_limit = false; // respektovat PLAYERSSTAT_LIMIT env
};

int RunJob(const JobConfig &config);

} // namespace PlayerStatProcessing

#endif
