//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#include <memory>
#include <iostream>
#include <storage_server.hpp>

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cout << "Usage: " << argv[0] << " <host> <port> <conf>" << std::endl;

        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];

    std::string conf = argv[3];

    kv::storage_server s(host, port, conf);

    if (s.init() == -1)
    {
        std::cout << "storage_server init failed" << std::endl;
        exit(-1);
    }

    s.start();

    return 0;
}
