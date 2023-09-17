//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef CONFIG_CLIENT_HPP
#define CONFIG_CLIENT_HPP

#include <memory>
#include <iostream>
#include <functional>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>

namespace draft
{
    namespace net = boost::asio;

    namespace beast = boost::beast;
    namespace http = beast::http;

    using tcp = net::ip::tcp;
    using endpoint_t = tcp::endpoint;

    using error_code_t = boost::system::error_code;
    using results_type = tcp::resolver::results_type;

    inline void fail(error_code_t ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << std::endl;
    }

    class config_client : public std::enable_shared_from_this<config_client>
    {
    public:
        explicit config_client(net::io_context& ioc) : resolver(ioc), stream(ioc)
        {
        }

        constexpr decltype(auto) shared_this()
        {
            return this->shared_from_this();
        }

        void run(const std::string& host, const std::string& port, const std::string& target, int version)
        {
            req.version(version);

            req.method(http::verb::get);
            req.target(target);

            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            resolver.async_resolve(host, port,
            [self = shared_this()](error_code_t ec, results_type endpoints)
            {
                self->on_resolve(ec, endpoints);
            });
        }

        void on_resolve(error_code_t ec, results_type endpoints)
        {
            if (ec)
                return fail(ec, "resolve");

            stream.expires_after(std::chrono::seconds(30));

            stream.async_connect(endpoints,
            [self = shared_this()](error_code_t ec, endpoint_t)
            {
                self->on_connect(ec);
            }); 
        }

        void on_connect(error_code_t ec)
        {
            if (ec)
                return fail(ec, "connect");

            stream.expires_after(std::chrono::seconds(30));

            http::async_write(stream, req,
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_write(ec, bytes_transferred);
            });
        }

        void on_write(beast::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec)
                return fail(ec, "write");

            http::async_read(stream, buffer_, res,
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_read(ec, bytes_transferred);
            }); 
        }

        void on_read(error_code_t ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec)
                return fail(ec, "read");

            decltype(auto) body = res.body();

            if (!body.empty())
                std::cout << body << std::endl;
        }

        void close()
        {
            error_code_t ec;
            auto& socket = stream.socket();

            socket.shutdown(tcp::socket::shutdown_both, ec);
            socket.close(ec);

            if (ec && ec != beast::errc::not_connected)
                return fail(ec, "shutdown");
        }

        ~config_client()
        {
            close();
        }

    private:
        tcp::resolver resolver;

        beast::tcp_stream stream;
        beast::flat_buffer buffer_;

        http::request<http::empty_body> req;
        http::response<http::string_body> res;
    };
}

#endif
