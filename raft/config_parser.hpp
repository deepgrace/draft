//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <fstream>
#include <unordered_map>
#include <boost/program_options.hpp>

namespace draft
{
    namespace po = boost::program_options;

    class config_parser
    {
    public:
        using config_t = std::unordered_map<std::string, std::string>;

        config_parser(const config_parser&) = default;
        config_parser(config_parser&&) = default;

        config_parser(const std::string& file) : filename_(file)
        {
        }

        rc_errno parse()
        {
            if (inited)
                return RC_GOOD;

            std::ifstream fin(filename_, std::ios_base::in);

            if (!fin.is_open())
                return RC_ERROR;

            po::variables_map vm;
            po::options_description desc;

            po::parsed_options parsed = po::parse_config_file(fin, desc, true);

            po::store(parsed, vm);
            po::notify(vm);

            auto ops = po::collect_unrecognized(parsed.options, po::include_positional);

            for (size_t i = 0; i < ops.size(); i += 2)
                 config_.emplace(ops[i], ops[i + 1]);

            inited = true;

            return RC_GOOD;
        }

        std::string* find(const std::string& key)
        {
            if (auto it = config_.find(key); it != config_.end())
                 return &it->second;

            return nullptr;
        }

        std::string& filename()
        {
            return filename_;
        }

        config_t& config()
        {
            return config_;
        }

    private:
        bool inited = false;

        config_t config_;
        std::string filename_;
    };
}

#endif
