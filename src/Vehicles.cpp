/*
 * Vehicles - Spracovanie encyklopedie vozidiel WoT
 *
 * Nacita zoznam vsetkych vozidiel z WoT API a ulozi ich do databazy
 * encyclopedia_vehicles. Pouziva paginated API endpoint.
 *
 * Author: Boris Fekar
 * Date: 2026-03-29
 */

#include <iostream>
#include <map>
#include <chrono>
#include <memory>
#include <sstream>
#include <iomanip>
#include "./lib/SendCurl.h"
#include "./lib/Pgsql.h"
#include "./lib/CronLog.h"
#include <nlohmann/json.hpp>

struct Vehicle {
    int         tank_id;
    std::string name;
    std::string tag;
    int         tier;
    std::string type;
    std::string nation;
    bool        is_premium;
    std::string small_icon;
    std::string contour_icon;
    std::string big_icon;
};

using VehicleMap = std::map<int, Vehicle>;

struct Stats {
    int total_vehicles = 0;
    int total_pages    = 0;
    int pages_fetched  = 0;
};

static void FetchPage(int page, nlohmann::json& out_data, Stats& stats)
{
    const std::string method    = "/encyclopedia/vehicles/";
    const std::string post_data = "&fields=tank_id,name,tag,tier,type,nation,is_premium,images"
                                  "&language=cs&page_no=" + std::to_string(page);

    SendCurl send;
    std::string raw = send.SendWOT(method, post_data);

    nlohmann::json js = nlohmann::json::parse(raw);
    stats.total_pages    = js["meta"]["page_total"].get<int>();
    stats.total_vehicles = js["meta"]["total"].get<int>();
    out_data = js["data"];
}

static void ParseVehicles(const nlohmann::json& data, VehicleMap& vehicles)
{
    for (const auto& [key, j] : data.items()) {
        Vehicle v;
        v.tank_id      = j["tank_id"].get<int>();
        v.name         = j["name"].get<std::string>();
        v.tag          = j["tag"].get<std::string>();
        v.tier         = j["tier"].get<int>();
        v.type         = j["type"].get<std::string>();
        v.nation       = j["nation"].get<std::string>();
        v.is_premium   = j["is_premium"].get<bool>();
        v.small_icon   = j["images"]["small_icon"].get<std::string>();
        v.contour_icon = j["images"]["contour_icon"].get<std::string>();
        v.big_icon     = j["images"]["big_icon"].get<std::string>();
        vehicles[v.tank_id] = v;
    }
}

static VehicleMap FetchAllVehicles(Stats& stats)
{
    VehicleMap vehicles;
    int page = 1;
    stats.total_pages = 2; // aktualizuje sa po prvom volani

    while (page <= stats.total_pages) {
        nlohmann::json data;
        FetchPage(page, data, stats);
        ParseVehicles(data, vehicles);
        stats.pages_fetched = page;
        page++;
    }
    return vehicles;
}

static bool PrepareInsertStatement(PGconn* conn)
{
    const char* sql =
        "INSERT INTO encyclopedia_vehicles "
        "(tank_id, name, tag, level, nation, type, is_premium, big_icon, small_icon, contour_icon) "
        "VALUES ($1::int, $2, $3, $4::int, $5, $6, $7::bool, $8, $9, $10)";

    PGresult* result = PQprepare(conn, "insert_vehicle", sql, 10, nullptr);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba PREPARE: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }
    PQclear(result);
    return true;
}

static bool SaveVehicles(const VehicleMap& vehicles, PGconn* conn)
{
    // Priprav INSERT statement
    if (!PrepareInsertStatement(conn)) {
        return false;
    }

    // Zacni transakciu
    PGresult* result = PQexec(conn, "BEGIN");
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba BEGIN: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }
    PQclear(result);

    // Vyprazdni tabulku
    result = PQexec(conn, "TRUNCATE encyclopedia_vehicles");
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba TRUNCATE: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        PQexec(conn, "ROLLBACK");
        return false;
    }
    PQclear(result);

    // Vloz vozidla cez prepared statement
    for (const auto& [id, v] : vehicles) {
        std::string tank_id_s    = std::to_string(v.tank_id);
        std::string tier_s       = std::to_string(v.tier);
        std::string is_premium_s = v.is_premium ? "true" : "false";

        const char* params[] = {
            tank_id_s.c_str(),
            v.name.c_str(),
            v.tag.c_str(),
            tier_s.c_str(),
            v.nation.c_str(),
            v.type.c_str(),
            is_premium_s.c_str(),
            v.big_icon.c_str(),
            v.small_icon.c_str(),
            v.contour_icon.c_str()
        };

        result = PQexecPrepared(conn, "insert_vehicle", 10, params, nullptr, nullptr, 0);
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::cerr << "Chyba INSERT tank_id=" << v.tank_id << ": "
                      << PQresultErrorMessage(result) << std::endl;
            PQclear(result);
            PQexec(conn, "ROLLBACK");
            return false;
        }
        PQclear(result);
    }

    // Potvrdi transakciu
    result = PQexec(conn, "COMMIT");
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba COMMIT: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        PQexec(conn, "ROLLBACK");
        return false;
    }
    PQclear(result);

    // VACUUM musi byt mimo transakcie
    result = PQexec(conn, "VACUUM FULL ANALYZE encyclopedia_vehicles");
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::cerr << "Chyba VACUUM: " << PQresultErrorMessage(result) << std::endl;
        PQclear(result);
        return false;
    }
    PQclear(result);

    return true;
}

int main()
{
    CronLog::ScopedRun cron_log("cplusplus:Vehicles");

    auto start = std::chrono::high_resolution_clock::now();

    // DB spojenie
    auto pg = std::make_unique<Pgsql>();
    PGconn* conn = pg->Get();

    if (conn == nullptr || PQstatus(conn) != CONNECTION_OK) {
        cron_log.SetInfo("ERROR: db connect failed");
        std::cerr << "Chyba: Nepodarilo sa pripojit k databaze!" << std::endl;
        return 1;
    }

    // Ziskaj data z API
    Stats stats;
    VehicleMap vehicles;
    try {
        vehicles = FetchAllVehicles(stats);
    }
    catch (const std::exception& e) {
        cron_log.SetInfo(std::string("ERROR: fetch failed: ") + e.what());
        std::cerr << "Chyba pri nacitavani dat z API: " << e.what() << std::endl;
        return 1;
    }

    // Uloz do databazy
    if (!SaveVehicles(vehicles, conn)) {
        cron_log.SetInfo("ERROR: save to db failed");
        return 1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "STATISTIKA SPRACOVANIA" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Celkovy cas:\t\t" << elapsed.count() << " s" << std::endl;
    std::cout << "Pocet vozidiel:\t\t" << vehicles.size() << std::endl;
    std::cout << "Pocet stranok API:\t" << stats.pages_fetched << std::endl;

    std::ostringstream info;
    info << std::fixed << std::setprecision(3);
    info << "OK"
         << " Cas=" << elapsed.count() << "s"
         << " Vozidiel=" << vehicles.size()
         << " Stranok=" << stats.pages_fetched;
    cron_log.SetInfo(info.str());

    return 0;
}
