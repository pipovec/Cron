/*
*   Jednoduchy loader DB konfiguracie zo suboru (.env styl).
*
*   Podporovane kluce:
*     - CONNINFO (plny libpq conninfo retazec)
*     - PGHOST, PGHOSTADDR, PGPORT, PGDATABASE, PGUSER, PGPASSWORD, PGSSLMODE
*     - alternativy: DB_HOST, DB_HOSTADDR, DB_PORT, DB_NAME/DB_DATABASE, DB_USER/DB_USERNAME, DB_PASSWORD
*
*   Subor sa hlada podla env premennej DB_CONFIG_PATH, inak .env v CWD.
*/

#ifndef DBCONFIG_H
#define DBCONFIG_H

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace DbConfig
{
    inline std::string GetConfigPath()
    {
        const char* env = std::getenv("DB_CONFIG_PATH");
        if (env && *env) {
            return std::string(env);
        }
        return ".env";
    }

    namespace detail
    {
        static inline std::string Trim(const std::string& input)
        {
            size_t start = 0;
            while (start < input.size() && (input[start] == ' ' || input[start] == '\t' || input[start] == '\r')) {
                start++;
            }
            size_t end = input.size();
            while (end > start &&
                   (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r')) {
                end--;
            }
            return input.substr(start, end - start);
        }

        static inline std::string StripQuotes(const std::string& input)
        {
            if (input.size() >= 2) {
                char first = input.front();
                char last = input.back();
                if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                    return input.substr(1, input.size() - 2);
                }
            }
            return input;
        }

        static inline bool GetValue(const std::unordered_map<std::string, std::string>& kv,
                                    const std::string& key,
                                    std::string* out)
        {
            auto it = kv.find(key);
            if (it == kv.end()) return false;
            *out = it->second;
            return true;
        }

        static inline void AppendKV(std::ostringstream& out, const std::string& key, const std::string& value)
        {
            if (value.empty()) return;
            if (out.tellp() > 0) out << " ";
            out << key << "=" << value;
        }
    }

    inline std::string LoadConninfo(const std::string& path, const std::string& fallback)
    {
        std::ifstream file(path);
        if (!file.is_open()) {
            return fallback;
        }

        std::unordered_map<std::string, std::string> kv;
        std::string line;

        while (std::getline(file, line)) {
            std::string trimmed = detail::Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            if (trimmed.rfind("export ", 0) == 0) {
                trimmed = detail::Trim(trimmed.substr(7));
            }

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) continue;

            std::string key = detail::Trim(trimmed.substr(0, eq));
            std::string value = detail::Trim(trimmed.substr(eq + 1));
            value = detail::StripQuotes(value);

            if (!key.empty()) {
                kv[key] = value;
            }
        }

        std::string conninfo;
        if (detail::GetValue(kv, "CONNINFO", &conninfo) && !conninfo.empty()) {
            return conninfo;
        }

        std::string host, hostaddr, port, dbname, user, password, sslmode;
        if (!detail::GetValue(kv, "PGHOST", &host)) detail::GetValue(kv, "DB_HOST", &host);
        if (!detail::GetValue(kv, "PGHOSTADDR", &hostaddr)) detail::GetValue(kv, "DB_HOSTADDR", &hostaddr);
        if (!detail::GetValue(kv, "PGPORT", &port)) detail::GetValue(kv, "DB_PORT", &port);
        if (!detail::GetValue(kv, "PGDATABASE", &dbname)) {
            if (!detail::GetValue(kv, "DB_NAME", &dbname)) {
                detail::GetValue(kv, "DB_DATABASE", &dbname);
            }
        }
        if (!detail::GetValue(kv, "PGUSER", &user)) {
            if (!detail::GetValue(kv, "DB_USER", &user)) {
                detail::GetValue(kv, "DB_USERNAME", &user);
            }
        }
        if (!detail::GetValue(kv, "PGPASSWORD", &password)) detail::GetValue(kv, "DB_PASSWORD", &password);
        detail::GetValue(kv, "PGSSLMODE", &sslmode);

        std::ostringstream out;
        detail::AppendKV(out, "host", host);
        detail::AppendKV(out, "hostaddr", hostaddr);
        detail::AppendKV(out, "port", port);
        detail::AppendKV(out, "dbname", dbname);
        detail::AppendKV(out, "user", user);
        detail::AppendKV(out, "password", password);
        detail::AppendKV(out, "sslmode", sslmode);

        conninfo = out.str();
        if (conninfo.empty()) {
            return fallback;
        }

        return conninfo;
    }
}

#endif
