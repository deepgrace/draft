//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#include <iomanip>
#include <iostream>
#include <severity_logger.hpp>
#include <entry_logger.hpp>

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cout << "usage: " << argv[0] << " <stat_file> <ldbs_file>" << std::endl;
        std::cout << "commands: exit | show | last | help | append <log> | chop <index> | drop <index>" << std::endl;

        return 1;
    }

    std::string stat_file = argv[1];
    std::string ldbs_file = argv[2];

    draft::entry_logger log(stat_file, ldbs_file);

    if (log.init() != RC_GOOD)
    {
        std::cerr << "failed to initialize log" << std::endl;

        return 1;
    }

    std::string command;

    while (std::cin >> command)
    {
        if (command == "exit")
            exit(0);
        else if (command == "show")
        {
            std::cout << "Len\t" << "Index\t" << "Term\t" << "Config\t" << "Data" << std::endl;

            for (auto it = log.begin(); it != log.end(); ++it)
            {
                 std::cout << it->len << "\t" << it->index << "\t" << it->term << "\t"
                           << it->cfg << "\t'" << it->str() << "'" << std::endl;
            }
        }
        else if (command == "chop" || command == "drop")
        {
            rc_errno rc;
            uint64_t index;

            std::cin >> index;

            if (command == "chop")
                rc = log.chop(index, true);
            else
                rc = log.drop(index, true);

            if (rc != RC_GOOD)
                std::cerr << "failed to " << command << " entry at index " << index << std::endl;
        }
        else if (command == "append")
        {
            std::string data;
            std::cin >> data;

            bool cfg = false;
            std::vector<std::string> v = draft::split(data, ",");

            for (auto& s : v)
                 cfg = draft::is_ipv4(s);

            auto entry = draft::make_log_entry(data.size(), cfg);

            entry->index = log.last_entry_index() + 1;
            entry->term = log.last_entry_term();

            entry->cfg = cfg;
            log.copy(entry.get(), data, cfg);

            if (log.append(entry, true) != RC_GOOD)
                std::cerr << "failed to append entry: " << draft::to_string(entry) << std::endl;
        }
        else if (command == "last")
        {
            std::cout << "last_index  " << log.last_index() << std::endl;
            std::cout << "last_term   " << log.last_term() << std::endl;

            std::cout << "last_config " << log.last_config() << std::endl;
        }
        else if (command == "help")
            std::cout << "commands: exit | show | last | help | append <log> | chop <index> | drop <index>" << std::endl;
    }

    return 0;
}
