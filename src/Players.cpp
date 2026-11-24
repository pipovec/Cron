/*
 * Players - Unifikovany program pre spracovanie hracov klanov
 *
 * Pouzitie: ./Players <TYP>
 *   TYP moze byt: CS, DE, PL, FR, ALL
 *
 * Priklad:
 *   ./Players CS  - spracuje ceske a Slovenske klany
 *   ./Players DE  - spracuje nemecke klany
 *   ./Players ALL - spracuje vsetky ostatne klany vratane PL, FR a bez jazyka (nie CS, DE)
 *
 * Author: Boris Fekar
 * Date: 2025-11-19
 */

#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <memory>
#include <iomanip>
#include <sstream>
#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include "./lib/QueryBuilder.h"
#include "./lib/PlayerProcessor.h"

using namespace PlayerProcessing;

void PrintUsage(const char* programName)
{
    std::cout << "Pouzitie: " << programName << " <TYP>" << std::endl;
    std::cout << std::endl;
    std::cout << "Platne typy:" << std::endl;
    std::cout << "  CS  - Ceske a Slovenske klany" << std::endl;
    std::cout << "  DE  - Nemecke klany" << std::endl;
    std::cout << "  PL  - Polske klany" << std::endl;
    std::cout << "  FR  - Francuzske klany" << std::endl;
    std::cout << "  ALL - Vsetky ostatne klany vratane PL, FR a bez jazyka (nie CS, DE)" << std::endl;
    std::cout << std::endl;
    std::cout << "Priklad:" << std::endl;
    std::cout << "  " << programName << " CS" << std::endl;
}

int main(int argc, char* argv[])
{
    CronLog::ScopedRun cron_log("cplusplus:Players");

    // Kontrola parametrov
    if (argc != 2) {
        cron_log.SetInfo("ERROR: invalid argc");
        std::cerr << "Chyba: Nespravny pocet parametrov!" << std::endl << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    std::string processType(argv[1]);

    // Validacia typu
    std::unique_ptr<QueryBuilder> queryBuilder;
    try {
        queryBuilder = std::make_unique<QueryBuilder>(processType);
    }
    catch (const std::invalid_argument& e) {
        cron_log.SetInfo("ERROR: invalid process type");
        std::cerr << "Chyba: " << e.what() << std::endl << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    // Zacni meranie casu
    auto start = std::chrono::high_resolution_clock::now();

    // Vytvor spojenie k databaze
    auto pg = std::make_unique<Pgsql>();
    PGconn* permanent_connection = pg->Get();

    if (permanent_connection == nullptr || PQstatus(permanent_connection) != CONNECTION_OK) {
        cron_log.SetInfo("ERROR: db connect failed");
        std::cerr << "Chyba: Nepodarilo sa pripojit k databaze!" << std::endl;
        return 1;
    }

    // Priprav prepared statements
    PreparedSelectPlayersAll(permanent_connection);
    PreparedSelectMembersRole(permanent_connection);

    // Inicializuj statistiku
    Statistika stat;
    stat.Reset();

    // Ziskaj vsetky clan_id
    // Rozpustene klany z clan_all_empty -> uzavriet members_role; omylom oznacene zive -> odznacit
    SkontrolujPrazdneKlany(permanent_connection, stat);

    ClanQueue cids;
    std::string query = queryBuilder->GetClanIdQuery();
    GetClanId(&cids, permanent_connection, query);

    // Spracovanie
    std::string json;
    std::string clan_id;
    int a_clan_id[100];
    int kolo = 0;

    while (!cids.empty()) {
        kolo++;

        // Ziskam string pre poslanie na server a pole intov
        Get100Id(&clan_id, &cids, a_clan_id);

        // Poslem udaje na server
        SendPostBatch(&clan_id, a_clan_id, &json, stat);

        // Spracuj data
        auto doit = std::make_unique<Spracuj>(a_clan_id, json, permanent_connection, stat);

        // Cleanup
        clan_id.clear();
        json.clear();
        ClearArray(a_clan_id);

        // Pauza pred dalsim kolom
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    // Vypis statistiku
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "STATISTIKA SPRACOVANIA" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Typ spracovania\t\t" << queryBuilder->GetProcessType() << std::endl;
    std::cout << "Pocet kol\t\t" << kolo << std::endl;
    std::cout << "Celkovy cas\t\t" << elapsed_seconds.count() << " s" << std::endl;
    std::cout << std::endl;
    stat.Print();

    std::ostringstream info;
    info << std::fixed << std::setprecision(3);
    info << "STATISTIKA SPRACOVANIA "
         << "Typ=" << processType
         << " Kola=" << kolo
         << " Cas=" << elapsed_seconds.count() << "s"
         << " Klany=" << stat.clans_processed
         << " Hraci=" << stat.players_checked
         << " Prichody=" << stat.new_players_all
         << " Odchody=" << stat.players_left
         << " NoveID=" << stat.new_account_id
         << " Nicky=" << stat.change_nick
         << " Roly=" << stat.change_role
         << " Ukoncene=" << stat.empty_clan
         << " RozpusteneZaznamy=" << stat.empty_rows_closed
         << " Odznacene=" << stat.empty_clans_unmarked
         << " Preskocene=" << stat.skipped_clans
         << " Updaty=0"
         << " Inserty=0"
         << " Spracovane=" << stat.players_checked
         << " MissingStats=0"
         << " MissingVehicles=0"
         << " Neaktivne=0";
    cron_log.SetInfo(info.str());
    return 0;
}
