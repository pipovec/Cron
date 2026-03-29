#ifndef CRON_LOG_H
#define CRON_LOG_H

#include "./Pgsql.h"
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

namespace CronLog
{
    inline std::string ClampVarchar255(const std::string& value, const std::string& fallback)
    {
        std::string out = value.empty() ? fallback : value;
        if (out.size() > 255) {
            out.resize(255);
        }
        return out;
    }

    inline std::string ToPgTimestamp(std::time_t ts)
    {
        std::tm tm_value;
#if defined(_WIN32)
        localtime_s(&tm_value, &ts);
#else
        localtime_r(&ts, &tm_value);
#endif
        char buffer[20];
        if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_value) == 0) {
            return "1970-01-01 00:00:00";
        }
        return std::string(buffer);
    }

    class Writer
    {
    public:
        explicit Writer(Pgsql* db = nullptr) : db_(db)
        {
            if (db_ == nullptr) {
                owned_db_.reset(new Pgsql());
                db_ = owned_db_.get();
            }
        }

        bool Insert(const std::string& start_time,
                    const std::string& end_time,
                    const std::string& name,
                    const std::string& info)
        {
            const std::string safe_name = ClampVarchar255(name, "unknown_cron");
            const std::string safe_info = ClampVarchar255(info, "no-info");

            const char* params[] = {
                start_time.c_str(),
                end_time.c_str(),
                safe_name.c_str(),
                safe_info.c_str()
            };

            PGresult* result = db_->QueryParams(
                "INSERT INTO cron_log (start_time, end_time, name, info) VALUES ($1, $2, $3, $4)",
                4,
                params
            );

            if (result == nullptr) {
                std::cerr << "CronLog insert failed: null result." << std::endl;
                return false;
            }

            const auto status = PQresultStatus(result);
            const bool ok = (status == PGRES_COMMAND_OK);
            if (!ok) {
                std::cerr << "CronLog insert failed: " << PQresultErrorMessage(result) << std::endl;
            }
            PQclear(result);
            return ok;
        }

        bool Insert(std::time_t start_time,
                    std::time_t end_time,
                    const std::string& name,
                    const std::string& info)
        {
            return Insert(ToPgTimestamp(start_time), ToPgTimestamp(end_time), name, info);
        }

    private:
        Pgsql* db_;
        std::unique_ptr<Pgsql> owned_db_;
    };

    class ScopedRun
    {
    public:
        explicit ScopedRun(const std::string& cron_name, Pgsql* db = nullptr)
            : writer_(db),
              name_(cron_name),
              info_("OK"),
              start_time_(std::time(nullptr)),
              flushed_(false),
              auto_flush_(true)
        {
        }

        void SetInfo(const std::string& info)
        {
            info_ = info;
        }

        void DisableAutoFlush()
        {
            auto_flush_ = false;
        }

        bool Flush()
        {
            if (flushed_) {
                return true;
            }

            flushed_ = writer_.Insert(start_time_, std::time(nullptr), name_, info_);
            return flushed_;
        }

        ~ScopedRun()
        {
            if (auto_flush_ && !flushed_) {
                (void)Flush();
            }
        }

    private:
        Writer writer_;
        std::string name_;
        std::string info_;
        std::time_t start_time_;
        bool flushed_;
        bool auto_flush_;
    };
}

#endif
