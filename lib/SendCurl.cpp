#include "./SendCurl.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdlib.h>
#include <chrono>
#include <thread>

// Limity v sekundach. Prepisatelne pri kompilacii (-DSENDCURL_TIMEOUT_SEC=...), pouziva test.
#ifndef SENDCURL_CONNECT_TIMEOUT_SEC
#define SENDCURL_CONNECT_TIMEOUT_SEC 15L
#endif
#ifndef SENDCURL_TIMEOUT_SEC
#define SENDCURL_TIMEOUT_SEC 120L
#endif

// Pocet pokusov SendWOT pri sietovej chybe (timeout, spojenie zlyhalo) a pauza medzi nimi
static const int kMaxNetAttempts = 3;
static const int kNetRetryPauseSec = 10;

SendCurl::SendCurl()
{
    // curl_global_init nie je thread-safe; C++11 zaruci jedno bezpecne vykonanie
    static const CURLcode kGlobalInit = curl_global_init(CURL_GLOBAL_DEFAULT);
    (void)kGlobalInit;
}

void SendCurl::ResetHandle()
{
    if (curl_ != nullptr)
    {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

int writer(char *data, size_t size, size_t nmemb, std::string *buffer)
{
    int result = 0;
    if (buffer != NULL)
    {
        buffer->append(data, size * nmemb);
        result = size * nmemb;
    }

    return result;
}

string SendCurl::SendRequest(const char *url, const char *postdata)
{
    string buffer;

    // Handle sa vytvara raz a znovupouziva - libcurl tak drzi spojenie so serverom otvorene
    // (keep-alive) a dalsie requesty nerobia novy TCP handshake.
    if (curl_ == nullptr)
    {
        curl_ = curl_easy_init();
        if (curl_ == nullptr)
        {
            throw string("Curl error: curl_easy_init zlyhal");
        }
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writer);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, SENDCURL_CONNECT_TIMEOUT_SEC);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, SENDCURL_TIMEOUT_SEC);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L); // timeouty bez signalov (bezpecne vo vlaknach)
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &buffer);

    CURLcode res = curl_easy_perform(curl_);

    if (res != CURLE_OK)
    {
        string error = string("Curl error: ") + curl_easy_strerror(res);
        ResetHandle(); // po chybe zacneme s cistym handle aj spojenim
        throw error;
    }

    return buffer;
}

string SendCurl::SendWGN(string method, string post)
{

    // Zmenim string na char
    string tmp_url = server_wgn + method;
    const char *url = tmp_url.c_str();

    // Zmenim string na char
    string tmp_post = api_id + post;
    const char *postdata = tmp_post.c_str();

    string SendRequest(const char *url, const char *postdata);

    string data;
    data = this->SendRequest(url, postdata);

    tmp_post.clear();
    tmp_url.clear();

    using json = nlohmann::json;
    json js;
    try
    {
        js = json::parse(data);
    }
    catch (json::parse_error &e)
    {
        cout << "Parser in SendCurl: " << e.what() << endl;
        cout << js << endl;
    }

    string status;

    status = js["status"].get<string>();

    if (status.compare("ok") != 0)
    {
        string fail = js["error"]["field"].get<string>() + ", " + js["error"]["message"].get<string>() + ": " + js["error"]["value"].get<string>();
        throw runtime_error(fail);
    }
    js.clear();

    return data;
}

string SendCurl::SendWOT(string method, string post)
{
    using namespace std;

    string data;
    // Treba premenit string na char
    string tmp_url = server_wot + method;
    const char *url = tmp_url.c_str();

    string tmp_post = api_id + post;
    const char *postdata = tmp_post.c_str();

    int net_failures = 0;

    while (true)
    {
        try
        {
            data = this->SendRequest(url, postdata);
        }
        catch (const string &e)
        {
            if (++net_failures >= kMaxNetAttempts)
            {
                throw; // po poslednom pokuse chybu posunieme volajucemu
            }
            cout << "SendWOT sietova chyba (" << e << "), pokus " << net_failures << "/" << kMaxNetAttempts
                 << ", cakam " << kNetRetryPauseSec << " sekund..." << endl;
            this_thread::sleep_for(chrono::seconds(kNetRetryPauseSec));
            continue;
        }

        using json = nlohmann::json;
        json js;

        try
        {
            js = json::parse(data);
        }
        catch (json::parse_error &e)
        {
            cout << "SendWOT parse error: " << e.what() << endl;
            cout << "Cakam 60 sekund a skusim znova..." << endl;
            this_thread::sleep_for(chrono::seconds(60));
            continue;
        }

        string status = js.value("status", "ok");

        if (status == "error")
        {
            cout << "API chyba: " << js << endl;
            cout << "Cakam 60 sekund a skusim znova..." << endl;
            js.clear();
            this_thread::sleep_for(chrono::seconds(60));
            continue;
        }

        js.clear();
        break;
    }

    tmp_post.clear();
    tmp_url.clear();

    return data;
}

bool SendCurl::SkontrolujStatus(string data)
{

    string tmp;
    unsigned zaciatok = data.find("status\":\"");
    unsigned koniec = data.find("\"", zaciatok + 9);

    tmp = data.substr(zaciatok + 9, koniec - (zaciatok + 9));

    if (tmp.compare("ok") == 0)
    {
        data.clear();
        return true;
    }
    else
    {
        cout << "Fail status: " << data << endl;
        data.clear();
        return false;
    }
}

SendCurl::~SendCurl()
{
    ResetHandle();
}
