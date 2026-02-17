#include "QueryBuilder.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>

QueryBuilder::QueryBuilder(const std::string& type) : processType(type)
{
    NormalizeProcessType();

    if (!IsValidProcessType(processType)) {
        throw std::invalid_argument("Neplatny typ spracovania: " + type +
            ". Platne typy su: CS, DE, PL, FR, ALL");
    }
}

bool QueryBuilder::IsValidProcessType(const std::string& type) const
{
    return (type == "CS" || type == "DE" || type == "PL" ||
            type == "FR" || type == "ALL");
}

void QueryBuilder::NormalizeProcessType()
{
    // Konvertuj na uppercase
    std::transform(processType.begin(), processType.end(),
                   processType.begin(), ::toupper);
}

std::string QueryBuilder::GetClanIdQuery() const
{
    std::string query;
    const std::string baseQuery = "SELECT DISTINCT(clan_id) FROM clan_all WHERE ";
    const std::string excludeEmpty = " AND clan_id NOT IN (SELECT clan_id FROM clan_all_empty)";
    const std::string orderBy = " ORDER BY clan_id DESC";

    if (processType == "CS") {
        // Ceska a Slovenska
        query = baseQuery +
                "(clan_id IN (SELECT clan_id FROM tmp_clans_cs) OR language='cs' OR language='sk')" +
                excludeEmpty + orderBy;
    }
    else if (processType == "DE") {
        // Nemecka
        query = baseQuery +
                "(language='de' OR clan_id IN (SELECT clan_id FROM tmp_clans_de))" +
                excludeEmpty + orderBy;
    }
    else if (processType == "PL") {
        // Polska
        query = baseQuery +
                "language='pl'" +
                excludeEmpty + orderBy;
    }
    else if (processType == "FR") {
        // Francuzska
        query = baseQuery +
                "language='fr'" +
                excludeEmpty + orderBy;
    }
    else if (processType == "ALL") {
        // Vsetky ostatne (nie CS, DE, PL, FR)
        query = baseQuery +
                "language != 'cs' AND language != 'de' AND language != 'pl' AND language != 'fr'" +
                excludeEmpty + orderBy;
    }

    return query;
}

std::string QueryBuilder::GetProcessType() const
{
    return processType;
}
