/*
 * PlayersDebug - DEBUG verzia programu pre spracovanie hracov klanov
 *
 * Pri detekcii prazdneho klanu vypise JSON a zastavi program
 *
 * Pouzitie: ./PlayersDebug <TYP>
 *   TYP moze byt: CS, DE, PL, FR, ALL
 *
 * Priklad:
 *   ./PlayersDebug CS  - spracuje ceske a Slovenske klany
 *   ./PlayersDebug DE  - spracuje nemecke klany
 *   ./PlayersDebug ALL - spracuje vsetky ostatne klany
 *
 * Author: Boris Fekar
 * Date: 2025-11-20
 */

#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <memory>
#include "./lib/Pgsql.h"
#include "./lib/QueryBuilder.h"
#include "./lib/PlayerProcessorDebug.h"

using namespace PlayerProcessingDebug;

void PrintUsage(const char* programName)
{
    std::cout << "DEBUG VERZIA - Vypise JSON pri prazdnom klane a zastavi sa" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "Pouzitie: " << programName << " <TYP>" << std::endl;
    std::cout << std::endl;
    std::cout << "Platne typy:" << std::endl;
    std::cout << "  CS  - Ceske a Slovenske klany" << std::endl;
    std::cout << "  DE  - Nemecke klany" << std::endl;
    std::cout << "  PL  - Polske klany" << std::endl;
    std::cout << "  FR  - Francuzske klany" << std::endl;
    std::cout << "  ALL - Vsetky ostatne klany (nie CS, DE, PL, FR)" << std::endl;
    std::cout << std::endl;
    std::cout << "Priklad:" << std::endl;
    std::cout << "  " << programName << " CS" << std::endl;
}

int main(int argc, char* argv[])
{
    // Kontrola parametrov
    if (argc != 2) {
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
        std::cerr << "Chyba: " << e.what() << std::endl << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    std::cout << "\n============================================================" << std::endl;
    std::cout << "  DEBUG VERZIA - Zastavenie pri detekcii prazdneho klanu" << std::endl;
    std::cout << "============================================================\n" << std::endl;

    // Zacni meranie casu
    auto start = std::chrono::high_resolution_clock::now();

    // Vytvor spojenie k databaze
    auto pg = std::make_unique<Pgsql>();
    PGconn* permanent_connection = pg->Get();

    if (permanent_connection == nullptr || PQstatus(permanent_connection) != CONNECTION_OK) {
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
    ClanQueue cids;
    std::string query = queryBuilder->GetClanIdQuery();
    GetClanId(&cids, permanent_connection, query);

    std::cout << "Pocet klanov na spracovanie: " << cids.size() << std::endl;
    std::cout << "Spracovavam...\n" << std::endl;

    // Spracovanie
    std::string json;
    std::string clan_id;
    int a_clan_id[100];
    int kolo = 0;

    while (!cids.empty()) {
        kolo++;
        std::cout << "Kolo " << kolo << " (zostava " << cids.size() << " klanov)..." << std::endl;

        // Ziskam string pre poslanie na server a pole intov
        Get100Id(&clan_id, &cids, a_clan_id);

        // Poslem udaje na server
        SendPost(clan_id, &json);

        // Spracuj data (tu sa program zastavi ak najde prazdny klan)
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

    return 0;
}
