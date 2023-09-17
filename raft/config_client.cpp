//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#include <vector>
#include <boost/program_options.hpp>
#include <config_client.hpp>
#include <utils.hpp>

namespace net = boost::asio;
namespace po = boost::program_options;

// ./reconfigure --leader_address=127.0.0.1:8080 --add_server=127.0.0.1:9090

int main(int argc, char* argv[])
{
    try
    {
        po::options_description desc("Allowed options");

        desc.add_options()
            ("help", "produce help message")
            ("show_states", "show states of the server")
            ("show_servers", "show servers in the cluster")
            ("append", po::value<std::string>(), "the log to append")
            ("leader_address", po::value<std::string>(), "set leader address")
            ("target_server", po::value<std::string>(), "target server address")
            ("add_servers", po::value<std::string>(), "comma separated server address")
            ("del_servers", po::value<std::string>(), "comma separated server address");

        po::variables_map vm;

        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (argc == 1 || vm.count("help"))
        {
            std::cout << desc << std::endl;

            return 0;
        }

        if (vm.count("leader_address"))
        {
            auto leader_address = vm["leader_address"].as<std::string>();
            auto pos = leader_address.find_first_of(":");

            auto host = leader_address.substr(0, pos);
            auto port = leader_address.substr(pos + 1);

            auto invoke = [&](const std::string& target)
            {
                net::io_context ioc;
                std::make_shared<draft::config_client>(ioc)->run(host, port, target, 11);

                ioc.run();
            };

            std::string change;
            std::string target = "/";

            if (vm.count("show_states"))
                target.append("show_states");
            else if (vm.count("show_servers"))
                target.append("show_servers");
            else if (vm.count("append"))
            {
                std::string content = vm["append"].as<std::string>();

                draft::replace_all(content, " ", "%20");
                target.append("append?content=" + content);
            }
            else if (vm.count("target_server"))
            {
                std::string target_server = vm["target_server"].as<std::string>();
                target.append("leadership_transfer?server=" + target_server);
            }
            else if (vm.count("add_servers"))
                change = "add_servers";
            else if (vm.count("del_servers"))
                change = "del_servers";

            if (target != "/")
                invoke(target);
            else
            {
                auto servers = draft::split(vm[change].as<std::string>(), ",");

                change.pop_back();
                target.append(change + "?server=");

                for (auto& s : servers)
                     invoke(target + s);
            }
        }
        else
            std::cout << "leader_address was not set" << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "Caught Exceptioon: " << e.what() << std::endl;
    }

    return 0;
}
