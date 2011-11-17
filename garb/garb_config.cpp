/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#include "garb_config.hpp"
#include "garb_logger.hpp"

#include <replicator.hpp>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <iostream>
#include <fstream>

namespace garb
{

    std::string const Config::DEFAULT_SST(GALERA_REPLICATOR_TRIVIAL_SST);

Config::Config (int argc, char* argv[]) throw (gu::Exception)
    : daemon_  (false),
      address_ (),
      group_   ("my_test_cluster"),
      sst_     (DEFAULT_SST),
      donor_   (),
      options_ (),
      log_     (),
      cfg_     ()
{
    po::options_description other ("Other options");
    other.add_options()
        ("version,v", "Print version")
        ("help,h",    "Show help message")
        ;

    // only these are read from cfg file
    po::options_description config ("Configuration");
    config.add_options()
        ("daemon,d", "Become daemon")
        ("address,a",po::value<std::string>(&address_), "Group address")
        ("group,g",  po::value<std::string>(&group_),   "Group name")
        ("sst",      po::value<std::string>(&sst_),     "SST request string")
        ("donor",    po::value<std::string>(&donor_),   "SST donor name")
        ("options,o",po::value<std::string>(&options_), "GCS/GCOMM option list")
        ("log,l",    po::value<std::string>(&log_),     "Log file")
        ;

    po::options_description cfg_opt;
    config.add_options()
        ("cfg,c",    po::value<std::string>(&cfg_),     "Configuration file")
        ;

    // these are accepted on the command line
    po::options_description cmdline_opts;
    cmdline_opts.add(config).add(cfg_opt).add(other);

    // we can submit address without option
    po::positional_options_description p;
    p.add("address", -1);

    po::variables_map vm;
    store(po::command_line_parser(argc, argv).
          options(cmdline_opts).positional(p).run(), vm);
    notify(vm);

    if (vm.count("help"))
    {
        std::cerr << "\nUsage: " << argv[0] << " [options] [group address]\n"
                  << cmdline_opts << std::endl;
        throw gu::Exception("Exit");
    }

    if (vm.count("version"))
    {
        log_info << GALERA_VER << ".r" << GALERA_REV;
    }

    if (vm.count("cfg"))
    {
        std::ifstream ifs(cfg_.c_str());

        if (!ifs.good())
        {
            gu_throw_error(ENOENT)
                << "Failed to open configuration file '" << cfg_
                << "' for reading.";
        }

        store(parse_config_file(ifs, config), vm);
        notify(vm);
    }

    if (!vm.count("address"))
    {
        gu_throw_error(EDESTADDRREQ) << "Group address not specified";
    }

    if (!vm.count("group"))
    {
        gu_throw_error(ENOKEY) << "Group name not specified";
    }

    if (vm.count("daemon"))
    {
        daemon_ = true;
    }

    if (options_.length() > 0) options_ += "; ";
    options_ += "gcs.fc_limit=9999999; gcs.fc_factor=1.0; gcs.fc_master_slave=yes";

    // this block must be the very last.
    gu_conf_self_tstamp_on();
    if (vm.count("log"))
    {
        set_logfile (log_);
    }
    else if (daemon_) /* if no log file given AND daemon operation requested -
                       * log to syslog */
    {
        gu_conf_self_tstamp_off();
        set_syslog();
    }
}

std::ostream& operator << (std::ostream& os, const Config& c)
{
    os << "\n\tdaemon:  " << c.daemon()
       << "\n\taddress: " << c.address()
       << "\n\tgroup:   " << c.group()
       << "\n\tsst:     " << c.sst()
       << "\n\tdonor:   " << c.donor()
       << "\n\toptions: " << c.options()
       << "\n\tcfg:     " << c.cfg()
       << "\n\tlog:     " << c.log();
    return os;
}

}
