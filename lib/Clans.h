#ifndef CLANS_H
#define CLANS_H

#include <iostream>

using namespace std;



/*
*   Trieda Clans je potom triedy SendCurl.
*   Pripaja sa na domenu WGN a ziskava udaje o klanoch 
*   pomoc motody Clans     
*
*   author: pipovec
*/

class Clans
{
    public:
            Clans();
            string ClansAll(string post);       // Informacia o klanoch, maximalne 100 klanov na dotaz
            string ClansDetails(string post);   // Detailne informacie o klane,
            string ClansMembers(string post);   // Informacie o hracoch klanu,
            string ClansGlossary(string post);  // Informacie o nastenke klanu,treba mat ID
            string ClansRatings(string post);   // Informacie o ratingu klanov,

    private:
            
            
            
            
            

};

#endif // CLANS_H