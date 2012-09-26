//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

//
// Small utility program to test cluster causality.
//
// For commandling options, run with --help.
//
// Build requirements:
// * C++11 capable compiler
// * gu_utils.hpp from galerautils
// * libmysql++ (libmysql++-dev on Ubuntu)
// * Boost program options
//
// Example build command with g++ 4.6:
//
// g++ -std=c++0x -g -O3 -Wextra -Wall -I../../galerautils/src
//     -I/usr/include/mysql++ -I/usr/include/mysql causal.cpp
//     -lboost_program_options -lmysqlpp -L../../galerautils/src -lgalerautils++
//      -o causal
//

#include "gu_utils.hpp"

#include <mysql++.h>

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstdlib>

#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

namespace causal
{

    // Commandline parsing and configuration
    class Config
    {
    public:
        Config(int argc, char* argv[])
            :
            db_           ("test"),
            read_hosts_   (),
            write_host_   ("localhost"),
            user_         ("test"),
            password_     ("testpass"),
            transactional_(false),
            duration_     (10),
            readers_      (1),
            disable_causal_reads_(false),
            compact_      (false),
            write_delay_  (0)
        {
            po::options_description other("Other options");
            other.add_options()
                ("help,h",  "Show help message")
                ("dry-run", "Print config and exit");

            std::string read_hosts_str("localhost");
            po::options_description config("Configuration");
            config.add_options()
                ("db",            po::value<std::string>(&db_),
                 "Database")
                ("read-host",     po::value<std::string>(&read_hosts_str),
                 "Read host (<host>:<port>)")
                ("write-host",    po::value<std::string>(&write_host_),
                 "Write host (<host>:<port>)")
                ("user"   ,       po::value<std::string>(&user_),
                 "User")
                ("password",      po::value<std::string>(&password_),
                 "Password")
                ("transactional", po::value<bool>(&transactional_),
                 "Transactional")
                ("duration",      po::value<time_t>(&duration_),
                 "Test duration in seconds")
                ("readers",       po::value<size_t>(&readers_),
                 "Number of reader threads")
                ("disable-causal-reads",
                 "Disable causal reads for reader connections")
                ("compact",       po::value<bool>(&compact_),
                 "Print output in one line")
                ("write-delay",   po::value<time_t>(&write_delay_),
                 "Delay between write operations in milliseconds");

            po::options_description opts;
            opts.add(config).add(other);

            po::variables_map vm;
            store(po::command_line_parser(argc, argv).options(opts).run(), vm);
            notify(vm);

            if (vm.count("disable-causal-reads"))
            {
                disable_causal_reads_ = true;
            }

            if (vm.count("help"))
            {
                std::cerr << "\nUsage: " << argv[0] << "\n"
                          << opts << std::endl;
                exit(EXIT_SUCCESS);
            }

            if (vm.count("dry-run"))
            {
                std::cerr << "Config: "
                          << "db            : " << db_            << "\n"
                          << "read-host     : " << read_hosts_str << "\n"
                          << "write-host    : " << write_host_    << "\n"
                          << "user          : " << user_          << "\n"
                          << "password      : " << password_      << "\n"
                          << "transactional : " << transactional_ << "\n"
                          << "duration      : " << duration_      << "\n"
                          << "readers       : " << readers_       << "\n"
                          << "disable-causal-reads: "
                          << disable_causal_reads_                << "\n"
                          << "compact       : " << compact_ << std::endl;
                exit(EXIT_SUCCESS);
            }

            read_hosts_ = gu::strsplit(read_hosts_str, ',');
        }

        const char* db() const { return db_.c_str(); }
        const char* read_host(size_t idx) const
        {
            return read_hosts_[idx % read_hosts_.size()].c_str();
        }

        const char* write_host() const { return write_host_.c_str(); }
        const char* user() const { return user_.c_str(); }
        const char* password() const { return password_.c_str(); }
        bool transactional() const { return transactional_; }
        time_t duration() const { return duration_; }
        size_t readers() const { return readers_; }
        bool disable_causal_reads() const { return disable_causal_reads_; }
        bool compact() const { return compact_; }
        time_t write_delay() const { return write_delay_; }
    private:
        std::string db_;
        std::vector<std::string> read_hosts_;
        std::string write_host_;
        std::string user_;
        std::string password_;
        bool transactional_;
        time_t duration_;
        size_t readers_;
        bool disable_causal_reads_;
        bool compact_;
        time_t write_delay_;
    };


    // Global state
    class Global
    {
    public:
        static std::atomic_llong violations_;
        static std::atomic_llong value_;
        static std::atomic_llong written_value_;
        static std::atomic_llong reads_;
        static std::atomic_llong failures_;
    };
    std::atomic_llong Global::violations_(0);
    std::atomic_llong Global::value_(0);
    std::atomic_llong Global::written_value_(0);
    std::atomic_llong Global::reads_(0);
    std::atomic_llong Global::failures_(0);

    // Reader class
    class Reader
    {
    public:
        Reader(const Config& config, size_t idx)
            :
            config_(config),
            conn_(config_.db(),
                  config_.read_host(idx),
                  config_.user(),
                  config_.password())
        {
            if (config.disable_causal_reads() == false)
            {
                (void)conn_.query("SET wsrep_causal_reads=1").execute();
            }
        }
        ~Reader()
        {
            conn_.disconnect();
        }

        Reader(const Reader&)         = delete;
        void operator=(const Reader&) = delete;


        long long value()
        {
            long long ret(-1);
            if (config_.transactional())
            {
                (void)conn_.query("START TRANSACTION").execute();
            }

            mysqlpp::StoreQueryResult result(
                conn_.query("SELECT value FROM causal_test").store());
            if (result.num_rows())
            {
                ret = gu::from_string<long long>(result[0]["value"].c_str());
            }
            else
            {
                throw std::runtime_error("select didn't result any value");
            }

            if (config_.transactional())
            {
                (void)conn_.query("COMMIT").execute();
            }

            return ret;
        }
    private:
        const Config& config_;
        mysqlpp::Connection conn_;
    };

    // Writer class
    class Writer
    {
    public:
        Writer(const Config& config)
            :
            config_(config),
            conn_(config_.db(),
                  config_.write_host(),
                  config_.user(),
                  config_.password())
        { }
        ~Writer()
        {
            conn_.disconnect();
        }
        Writer(const Writer&)         = delete;
        void operator=(const Writer&) = delete;

        void store_value(long long val)
        {
            std::ostringstream os;
            os << "UPDATE causal_test SET value = " << val;
            mysqlpp::Query query(conn_.query(os.str()));
            mysqlpp::SimpleResult result(query.execute());
            if (!result)
            {
                throw std::runtime_error("failed to store value");
            }
        }
    private:
        const Config& config_;
        mysqlpp::Connection conn_;
    };


    void init(const char* db, const char* server, const char* user,
              const char* password)
    {
        mysqlpp::Connection conn(db, server, user, password);
        (void)conn.query("DROP TABLE IF EXISTS causal_test").execute();
        (void)conn.query(
            "CREATE TABLE causal_test (value BIGINT PRIMARY KEY)")
            .execute();
        std::ostringstream os;
        os << "INSERT INTO causal_test VALUES ("
           << Global::value_.fetch_add(1LL)
           << ")";
        (void)conn.query(os.str()).execute();
        conn.disconnect();
    }

}


void writer_func(std::shared_ptr<causal::Writer> w,
                 const causal::Config& config)
{
    std::chrono::system_clock::time_point until(
        std::chrono::system_clock::now()
        + std::chrono::seconds(config.duration()));
    while (std::chrono::system_clock::now() < until)
    {
        long long val(causal::Global::value_.load());
        w->store_value(val);
        causal::Global::written_value_.store(val);
        ++causal::Global::value_;
        std::this_thread::sleep_for(std::chrono::milliseconds(config.write_delay()));
    }
}


void reader_func(std::shared_ptr<causal::Reader> r,
                 const causal::Config& config)
{
    std::chrono::system_clock::time_point until(
        std::chrono::system_clock::now()
        + std::chrono::seconds(config.duration()));
    try
    {
        while (std::chrono::system_clock::now() < until)
        {
            long long expected(causal::Global::written_value_.load());
            long long val(r->value());
            if (val < expected)
            {
                ++causal::Global::violations_;
            }
            ++causal::Global::reads_;
        }
    }
    catch (...)
    {
        std::cerr << "reader failed" << std::endl;
        ++causal::Global::failures_;
    }
}



int main(int argc, char* argv[])
{
    causal::Config config(argc, argv);
    causal::init(config.db(), config.write_host(),
                 config.user(), config.password());

    std::thread writer_thd(
        std::bind(
            writer_func,
            std::shared_ptr<causal::Writer>(new causal::Writer(config)),
            config));

    std::list<std::thread> reader_thds;
    for (size_t i(0); i < config.readers(); ++i)
    {
        reader_thds.push_back(
            std::thread(
                std::bind(
                    reader_func,
                    std::shared_ptr<causal::Reader>(
                        new causal::Reader(config, i)),
                    config)));
    }

    writer_thd.join();
    std::for_each(reader_thds.begin(), reader_thds.end(),
                  [](std::thread& thd) { thd.join(); });

    long long reads(causal::Global::reads_.load());
    long long violations(causal::Global::violations_.load());
    if (config.compact() == true)
    {
        std::cout << "Reads "       << reads
                  << " Violations " << violations
                  << " Writes " << causal::Global::value_.load()
                  << " Failures " << causal::Global::failures_.load()
                  << std::endl;
    }
    else
    {
        std::cout << "Reads            : " << reads << "\n"
                  << "Causal violations: " << violations << "\n"
                  << "Fraction         : "
                  << (double)violations/(reads == 0 ? 1 : reads)
                  << std::endl;
    }
    exit(EXIT_SUCCESS);
}
