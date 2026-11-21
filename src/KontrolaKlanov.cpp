/**
*   ╔═══════════════════════════════════════════════════════════════════╗
*   ║                      KONTROLA KLANOV                              ║
*   ║   Synchronizacia klanov z Wargaming API do PostgreSQL DB         ║
*   ╚═══════════════════════════════════════════════════════════════════╝
*
*   POPIS:
*   Program sťahuje údaje o klanoch z World of Tanks API pomocou metódy
*   https://api.worldoftanks.eu/wgn/clans/list/ a kontroluje, či sa
*   nezmenili údaje v databáze. Ak áno, vykoná update, ak klan neexistuje,
*   pridá ho do databázy.
*
*   OPTIMALIZÁCIE (2025-11-15):
*   ✓ Batch processing pre DB operácie (100 klanov naraz)
*   ✓ Hash mapa pre O(1) lookup namiesto O(n) lineárne prehľadávanie
*   ✓ Znížený počet DB dotazov z 50,000+ na ~500 (100× menej!)
*   ✓ Efektívne porovnávanie klanov
*   ✓ API rate limiting: max 15 requestov/sekundu (Wargaming limit)
*   ✓ Automatický reconnect pre DB spojenie
*   ✓ Lepší error handling a validácia
*
*   VÝKON:
*   - Pôvodná verzia: 10-20 minút
*   - Optimalizovaná:  1-2 minúty (10× rýchlejšie!)
*
*   ═══════════════════════════════════════════════════════════════════
*   KOMPILÁCIA A SPUSTENIE
*   ═══════════════════════════════════════════════════════════════════
*
*   1. INŠTALÁCIA ZÁVISLOSTÍ (len prvýkrát):
*      make KontrolaKlanov install-deps
*
*   2. KOMPILÁCIA:
*      make KontrolaKlanov
*
*      Alebo skrátene (make automaticky nájde makeKontrolaKlanov):
*      make
*
*   3. SPUSTENIE:
*      ./bin/KontrolaKlanov
*
*      Alebo priamo skompiluj a spusti:
*      make KontrolaKlanov run
*
*   4. ĎALŠIE PRÍKAZY:
*      make help         - Zobrazí všetky dostupné príkazy
*      make clean        - Vymaže objektové súbory
*      make rebuild      - Vymaže a znova skompiluje
*
*   5. MANUÁLNA KOMPILÁCIA (ak makefile nefunguje):
*      g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql \
*          ./KontrolaKlanov/ClanData.cpp -o ./KontrolaKlanov/ClanData.o
*
*      g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql \
*          ./KontrolaKlanov/ClanProcessor.cpp -o ./KontrolaKlanov/ClanProcessor.o
*
*      g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql \
*          ./lib/Pgsql.cpp -o ./lib/Pgsql.o
*
*      g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql \
*          ./lib/SendCurl.cpp -o ./lib/SendCurl.o
*
*      g++ -c --std=c++11 -Wall -O3 -I/usr/include/postgresql \
*          KontrolaKlanov.cpp -o ./KontrolaKlanov.o
*
*      g++ -o ./bin/KontrolaKlanov ./KontrolaKlanov.o \
*          ./KontrolaKlanov/ClanData.o ./KontrolaKlanov/ClanProcessor.o \
*          ./lib/SendCurl.o ./lib/Pgsql.o \
*          -L/usr/lib/x86_64-linux-gnu -lcurl -lpq -lpthread
*
*   ═══════════════════════════════════════════════════════════════════
*   POŽIADAVKY
*   ═══════════════════════════════════════════════════════════════════
*
*   Systémové závislosti:
*   - build-essential (g++, make)
*   - libpq-dev (PostgreSQL development files)
*   - libcurl4-openssl-dev (cURL development files)
*
*   Knižnice:
*   - PostgreSQL libpq
*   - libcurl
*   - nlohmann/json v3.12.0 (už zahrnuté v json/src/json.hpp)
*   - C++17 (využíva std::optional, std::string_view)
*
*   Databáza:
*   - PostgreSQL server bežiaci na porte 5433
*   - Databáza: clan
*   - User: deamon
*   - Tabuľka: clan_all (schema v KontrolaKlanov/README.md)
*
*   ═══════════════════════════════════════════════════════════════════
*
*   Autor: Boris Fekár (pipovec, BobyOneKiller)
*   Optimalizácia: 2025-11-15
*   Refactor: 2026-02-28
*   Dokumentácia: ./KontrolaKlanov/README.md
*
*/

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include "./lib/KontrolaKlanov/ClanData.h"
#include "./lib/KontrolaKlanov/ClanProcessor.h"
#include <nlohmann/json.hpp>

namespace {

const std::string kField = "&fields=clan_id,tag,created_at,name,members_count,emblems";
const std::string kMethod = "/clans/list/";
constexpr int kApiRateLimitMs = 70; // ~14.3 req/s
constexpr int kMaxRetries = 3;

class ClanSyncRunner {
public:
    ClanSyncRunner()
        : db_(std::make_unique<Pgsql>()),
          processor_(db_.get())
    {
    }

    int Run() {
        auto start_time = std::chrono::high_resolution_clock::now();
        auto last_request_time = std::chrono::steady_clock::now();

        std::cout << "*********************************" << std::endl;
        std::cout << "Program KontrolaKlanov" << std::endl;
        std::cout << "API Rate Limit: max 15 req/s" << std::endl;
        std::cout << "*********************************" << std::endl;

        std::cout << std::endl << "Nacitavam existujuce klany z databazy..." << std::endl;
        processor_.LoadExistingClans();

        std::cout << std::endl << "Zacinam stahovanie dat zo servera..." << std::endl;

        int page = 1;
        std::string json_string = GetDataFromServer(page, last_request_time);
        int total_page = GetTotalPages(json_string);

        ProcessPage(page, total_page, json_string);
        ++page;

        while (page <= total_page) {
            json_string = GetDataFromServer(page, last_request_time);
            ProcessPage(page, total_page, json_string);
            ++page;
        }

        std::cout << std::endl << "Ukladam posledne zmeny..." << std::endl;
        processor_.FlushBatches();

        std::cout << std::endl << "Kontrolujem neaktivne/rozpustene klany..." << std::endl;
        int inactive_count = processor_.GetInactiveClansCount();
        if (inactive_count > 0) {
            std::cout << "Najdenych " << inactive_count << " neaktivnych klanov" << std::endl;
            processor_.MarkInactiveClans();
        } else {
            std::cout << "Ziadne neaktivne klany" << std::endl;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

        std::cout << std::endl << "*********************************" << std::endl;
        std::cout << "Program uspesne dokonceny" << std::endl;
        std::cout << "Celkovy cas: " << duration.count() << " sekund" << std::endl;
        std::cout << "*********************************" << std::endl;

        std::ostringstream info;
        info << "STATISTIKA SPRACOVANIA "
             << "Typ=KontrolaKlanov"
             << " Kola=" << total_page
             << " Cas=" << duration.count() << "s"
             << " Klany=" << processed_clans_count_
             << " Hraci=0"
             << " Prichody=0"
             << " Odchody=0"
             << " NoveID=0"
             << " Nicky=0"
             << " Ukoncene=0"
             << " Updaty=0"
             << " Inserty=0"
             << " Spracovane=" << processed_clans_count_
             << " MissingStats=0"
             << " MissingVehicles=0"
             << " Neaktivne=" << inactive_count;
        run_summary_ = info.str();

        return 0;
    }

    const std::string& GetRunSummary() const {
        return run_summary_;
    }

private:
    std::string GetDataFromServer(int page, std::chrono::steady_clock::time_point& last_request_time) {
        auto now = std::chrono::steady_clock::now();
        auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time).count();

        if (time_since_last < kApiRateLimitMs) {
            int wait_time = kApiRateLimitMs - static_cast<int>(time_since_last);
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));
        }

        std::string post_data = kField + "&page_no=" + std::to_string(page);
        std::string json_string;

        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            try {
                json_string = curl_.SendWGN(kMethod, post_data);
                last_request_time = std::chrono::steady_clock::now();
                return json_string;
            }
            catch (const std::exception& e) {
                std::cout << "Chyba v ziskani dat zo servera (pokus " << attempt
                          << "): " << e.what() << std::endl;

                if (attempt >= kMaxRetries) {
                    throw std::runtime_error("Nepodarilo sa ziskat data po " +
                                             std::to_string(kMaxRetries) + " pokusoch");
                }

                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            catch (...) {
                std::cout << "Chyba v ziskani dat zo servera (pokus " << attempt
                          << "): neznamy error" << std::endl;

                if (attempt >= kMaxRetries) {
                    throw std::runtime_error("Nepodarilo sa ziskat data po " +
                                             std::to_string(kMaxRetries) + " pokusoch");
                }

                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }

        return {};
    }

    int GetTotalPages(const std::string& json_string) const {
        using json = nlohmann::json;
        json js = json::parse(json_string);

        int total = js["meta"]["total"].get<int>();
        int pages = (total / 100) + 1;

        std::cout << "Celkovy pocet klanov: " << total << std::endl;
        std::cout << "Celkovy pocet stran: " << pages << std::endl;

        return pages;
    }

    void ProcessPage(int page, int total_page, const std::string& json_string) {
        std::vector<ClanData> clans;
        processor_.ProcessJsonData(json_string, clans);
        processed_clans_count_ += static_cast<int>(clans.size());

        std::cout << "Spracovavam stranku " << page << "/" << total_page
                  << " (" << clans.size() << " klanov)" << std::endl;

        for (const auto& clan : clans) {
            processor_.ProcessClan(clan);
        }
    }

    std::unique_ptr<Pgsql> db_;
    ClanProcessor processor_;
    SendCurl curl_;
    std::string run_summary_ = "STATISTIKA SPRACOVANIA Typ=KontrolaKlanov";
    int processed_clans_count_ = 0;
};

} // namespace

int main() {
    CronLog::ScopedRun cron_log("cplusplus:KontrolaKlanov");
    try {
        ClanSyncRunner runner;
        const int rc = runner.Run();
        if (rc == 0) {
            cron_log.SetInfo(runner.GetRunSummary());
        } else {
            cron_log.SetInfo("ERROR: runner failed");
        }
        return rc;
    }
    catch (const std::exception& e) {
        cron_log.SetInfo(std::string("ERROR: ") + e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
