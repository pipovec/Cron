#ifndef SENDCURL_H
#define SENDCURL_H

#include <iostream>
#include <curl/curl.h>
#include <unistd.h> // sleep()


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
            string SendWGN(string method, string post);
            string SendWOT(string method, string post);
            ~SendCurl();
            
    
    private:
            string SendRequest(const char *url, const char *postdata);
            bool SkontrolujStatus(string data);    
            const string server_wgn   = "http://api.worldoftanks.eu/wgn";
            const string server_wot   = "http://api.worldoftanks.eu/wot"; 
            const string api_id       = "application_id=c428e2923f3d626de8cbcb3938bb68f8";
            
            
            
            

};

#endif // SENDCURL_H