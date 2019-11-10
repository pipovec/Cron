#include "./SendCurl.h"
#include "./json.hpp"
#include <iostream>
#include <chrono>
#include <thread>

SendCurl::SendCurl()
{
    // ctor
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

string SendCurl::SendRequest(string url, string postdata)
{

    CURL *curl;
    CURLcode res;
    string buffer;

    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));


        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        cout << "CURL response code: " << response_code << endl;
    }
    /* always cleanup */
    curl_easy_cleanup(curl);
    url.clear();
    postdata.clear();

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
    method.clear();

    string tmp_post = api_id + post;
    post.clear();

    int control = 1;

    using json = nlohmann::json;
    json js;
    string status;

    do
    {

        data = this->SendRequest(tmp_url, tmp_post);

        try
        {
            js = json::parse(data);
        }
        catch (json::parse_error &e)
        {
            cout << "Parser in SendCurl: " << e.what() << endl;
            cout << data << endl;
        }

        status = js["status"];

        if (status == "error")
        {
            cout << "Chyba send wot! :" << js << endl;
            control = 1;

            data.clear();
            js.clear();
            status.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            cout << "Spal som 2 sekundy" << endl;
        }
        else
        {
            control = 0;
            tmp_post.clear();
            tmp_url.clear();            
        }

        js.clear();
        status.clear();

    } while (control == 1);

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
}
