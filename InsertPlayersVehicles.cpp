#include <iostream>
#include <libpq-fe.h>
#include <queue>
#include "./lib/GetAccountId.h"

/** Kontajner na account_id */
std::queue<int>ids;

int main()
{    
    GetAccountId *getIds = new GetAccountId("Select account_id FROM players_all LIMIT 500");
    ids = getIds->dataset();
    printf("Pocet riadkov: %i \n", getIds->rowCount());
    delete getIds;

    std::cout << "Prvy prvok: " << ids.front() << std::endl;

    return 10;
}