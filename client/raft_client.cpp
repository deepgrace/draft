//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#include <raft_client.hpp>

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <host> <port>" << std::endl;

        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];

    std::string command;
    net::io_context ioc;

    raft_client c(ioc, host, port);
    std::thread t([&ioc]{ ioc.run(); });

    while (std::getline(std::cin, command))
    {
        if (!command.empty())
            c.write(command.append("\r\n"));
    }

    c.close();
    t.join();

    return 0;
}
