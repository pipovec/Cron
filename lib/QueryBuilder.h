/*
 * QueryBuilder - Trieda pre generovanie SQL dotazov pre rozne typy spracovania
 *
 * Podporuje: CS, DE, PL, FR, ALL
 *
 * Author: Boris Fekar
 * Date: 2025-11-19
 */

#ifndef QUERYBUILDER_H
#define QUERYBUILDER_H

#include <string>

class QueryBuilder
{
public:
    // Konstruktor prijma typ spracovania (CS, DE, PL, FR, ALL)
    QueryBuilder(const std::string& processType);

    // Vygeneruje SQL query pre vyber clan_id
    std::string GetClanIdQuery() const;

    // Vrati nazov typu spracovania
    std::string GetProcessType() const;

private:
    std::string processType;

    // Pomocne funkcie pre validaciu
    bool IsValidProcessType(const std::string& type) const;
    void NormalizeProcessType();
};

#endif
