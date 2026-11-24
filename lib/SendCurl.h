#ifndef SENDCURL_H
#define SENDCURL_H

#include <iostream>
#include <curl/curl.h>
#include <unistd.h> // sleep()
#include "./DbConfig.h"


using namespace std;



/*
*   Trieda na ziskavanie dat zo servera pomocou curl
*   pripaja sa na domenu wot aj wgn    
*   author: pipovec
*/

class SendCurl
{
    public:
            SendCurl();
            // Instancia drzi jeden curl handle (keep-alive medzi volaniami), preto sa nesmie
            // kopirovat ani zdielat medzi vlaknami.
            SendCurl(const SendCurl &) = delete;
            SendCurl &operator=(const SendCurl &) = delete;
            string SendWGN(string method, string post);
            string SendWOT(string method, string post);
            ~SendCurl();
            
    
    private:
            string SendRequest(const char *url, const char *postdata);
            bool SkontrolujStatus(string data);
            void ResetHandle();
            CURL *curl_ = nullptr;   // znovupouzivany handle, vytvori sa pri prvom requeste
            const string server_wgn   = "https://api.worldoftanks.eu/wgn";
            const string server_wot   = "https://api.worldoftanks.eu/wot"; 
            // WG application_id z .env (WG_APPLICATION_ID) alebo z premennej prostredia
            const string api_id       = "application_id=" + DbConfig::Get("WG_APPLICATION_ID");
            
            
            
            

};

#endif // SENDCURL_H