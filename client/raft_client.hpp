//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef RAFT_CLIENT_HPP
#define RAFT_CLIENT_HPP

#include <array>
#include <deque>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <functional>
#include <system_error>
#include <boost/asio.hpp>

namespace net = boost::asio;

using tcp = net::ip::tcp;
using socket_t = tcp::socket;

using endpoint_t = tcp::endpoint;
using buffer_t = std::array<char, 8192>;

using error_code_t = boost::system::error_code;
using results_type = tcp::resolver::results_type;

class raft_client
{
    public:
        raft_client(net::io_context& ioc, const std::string& host, const std::string& port) :
        ioc(ioc), resolver(ioc), socket(ioc), host(host), port(port)
        {
            do_resolve(host, port);
        }

        void do_resolve(const std::string& host, const std::string& port)
        {
            resolver.async_resolve(host, port,
            [this](error_code_t ec, const results_type& endpoints)
            {
                on_resolve(ec, endpoints);
            });
        }

        void on_resolve(error_code_t ec, const results_type& endpoints)
        {
            if (!ec)
                do_connect(endpoints);
            else
                close();
        }

        void do_connect(const results_type& endpoints)
        {
            net::async_connect(socket, endpoints,
            [this](error_code_t ec, endpoint_t)
            {
                on_connect(ec);
            });
        }

        void on_connect(error_code_t ec)
        {
            if (!ec)
            {
                if (!prev_command.empty())
                    write(prev_command);

                do_read_message();
            }
            else
                close();
        }

        void do_read_message()
        {
            socket.async_read_some(net::buffer(buffer_),
            [this](error_code_t ec, size_t bytes_transferred)
            {
                on_read_message(ec, bytes_transferred);
            });
        }

        void on_read_message(error_code_t ec, size_t bytes_transferred)
        {
            if (!ec)
            {
                std::string reply;

                if (bytes_transferred > 2)
                    reply = std::string(buffer_.data(), bytes_transferred - 2);

                std::cout << "(server " << host << ":" << port << ") " << reply << " ";

                if (auto i = reply.find(" "); i != std::string::npos)
                {
                    if (reply.substr(0, i) == "NOT_LEADER")
                    {
                        std::string leader = reply.substr(i + 1);

                        if (auto j = leader.find(":"); j != std::string::npos)
                        {
                            auto old_addr = host + ":" + port;

                            host = leader.substr(0, j);
                            port = leader.substr(j + 1);

                            std::replace(port.begin(), port.end(), '8', '9');
                            auto new_addr = host + ":" + port;

                            if (old_addr == new_addr)
                            {
                                std::cout << old_addr << " has been removed from the cluster" << std::endl;
                                exit(1);
                            }

                            std::cout << "redirect to " << host << ":" << port << std::endl;

                            close();
                            do_resolve(host, port);

                            return;
                        }

                        std::cout << "can't find leader address" << std::endl;
                    }
                }

                std::cout << std::endl;
                do_read_message();
            }
            else
                close();
        }

        void write(const std::string& command)
        {
            net::post(ioc,
            [this, command]
            {
                bool write_in_progress = !commands.empty();
                commands.push_back(command);

                if (!write_in_progress)
                    do_write();
            });
        }

        void do_write()
        {
            net::async_write(socket, net::buffer(commands.front()),
            [this](error_code_t ec, size_t bytes_transferred)
            {
                on_write(ec, bytes_transferred);
            });
        }

        void on_write(error_code_t ec, size_t bytes_transferred)
        {
            if (!ec)
            {
                auto& c = commands.front();
                prev_command = c;

                commands.pop_front();

                if (!commands.empty())
                    do_write();
            }
            else
                close();
        }

        void close()
        {
            error_code_t ec;
            socket.cancel(ec);

            socket.shutdown(tcp::socket::shutdown_both, ec);
            socket.close(ec);
        }

    private:
        net::io_context& ioc;
        tcp::resolver resolver;

        buffer_t buffer_;
        socket_t socket;

        std::string host;
        std::string port;

        std::string prev_command;
        std::deque<std::string> commands;
};

#endif
