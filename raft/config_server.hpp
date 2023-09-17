//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef CONFIG_SERVER_HPP
#define CONFIG_SERVER_HPP

#include <set>
#include <thread>
#include <iostream>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace draft
{
    namespace net = boost::asio;

    namespace beast = boost::beast;
    namespace http = beast::http;

    using tcp = net::ip::tcp;
    using error_code_t = boost::system::error_code;

    using request = http::request<http::string_body>;
    using local_socket = net::local::stream_protocol::socket;

    struct response
    {
        void do_write()
        {
            ready = true;

            if (writer && reader && writer->is_open() && reader->is_open())
            {
                writer->async_write_some(net::buffer(&ready, sizeof(ready)),
                [this](const error_code_t& ec, std::size_t bytes_transferred)
                {
                    on_write_some(ec, bytes_transferred);
                });
            }
        }

        void on_write_some(const error_code_t& ec, std::size_t bytes_transferred)
        {
        }

        std::shared_ptr<local_socket> reader;
        std::shared_ptr<local_socket> writer;

        bool ready = true;
        http::response<http::string_body> res;
    };

    template <typename T>
    void fail(const http::request<T>& req, response& rep, const std::string& target, http::status status)
    {
        http::response<T> res(status, req.version());

        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");

        res.keep_alive(req.keep_alive());

        res.body() = target;
        res.prepare_payload();

        rep.res = res;
    }

    template <typename T>
    class session : public std::enable_shared_from_this<session<T>>
    {
    public:
        session(T& t, tcp::socket socket) : t(t), stream(std::move(socket))
        {
            rep.reader = std::make_shared<local_socket>(stream.get_executor());
            rep.writer = std::make_shared<local_socket>(stream.get_executor());

            net::local::connect_pair(*rep.reader, *rep.writer);
        }

        constexpr decltype(auto) shared_this()
        {
            return this->shared_from_this();
        }

        void run()
        {
            net::dispatch(stream.get_executor(),
            [self = shared_this()]
            {
                self->do_read();
            });
        }

        void do_read()
        {
            req = {};

            stream.expires_after(std::chrono::seconds(30));

            http::async_read(stream, buffer_, req,
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_read(ec, bytes_transferred);
            });
        }

        void on_read(error_code_t ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec)
            {
                close();

                return;
            }

            t.dispatch(req, rep);

            if (rep.ready)
                do_write();
            else
            {
                rep.reader->async_read_some(net::buffer(&rep.ready, sizeof(rep.ready)),
                [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
                {
                    self->on_read_some(ec, bytes_transferred);
                });
            }
        }

        void on_read_some(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
                do_write();
        }

        void do_write()
        {
            auto keep_alive = rep.res.keep_alive();
            auto msg = http::message_generator(std::move(rep.res));

            beast::async_write(stream, std::move(msg),
            [self = shared_this(), keep_alive](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_write(keep_alive, ec, bytes_transferred);
            });
        }

        void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);

            if (ec || !keep_alive)
            {
                close();

                return;
            }

            do_read();
        }

        void close()
        {
            t.remove(shared_this());

            error_code_t ec;
            auto& socket = stream.socket();

            socket.shutdown(tcp::socket::shutdown_both, ec);
            socket.close(ec);
        }

        T& t;

        request req;
        response rep;

        beast::tcp_stream stream;
        beast::flat_buffer buffer_;
    };

    class config_server
    {
    public:
        using connection = session<config_server>;
        using connection_ptr = std::shared_ptr<connection>;

        using connections_t = std::set<connection_ptr>;

        using request_handler = std::function<void(const request& req, response& rep)>;
        using request_handlers_t = std::unordered_map<std::string, request_handler>;

        config_server(net::io_context& ioc, const std::string& port) :
        ioc(ioc), acceptor(ioc, tcp::endpoint(tcp::v4(), std::stoi(port)))
        {
        }

        config_server(net::io_context& ioc, const std::string& host, const std::string& port) :
        ioc(ioc), acceptor(ioc, tcp::endpoint(net::ip::make_address(host), std::stoi(port)))
        {
        }

        void run()
        {
            do_accept();
        }

        void stop()
        {
            acceptor.close();

            for (auto& s: connections)
                 s->close();

            connections.clear();
        }

        void remove(connection_ptr c)
        {
            connections.erase(c);
        }

        void do_accept()
        {
            acceptor.async_accept(
            [this](error_code_t ec, tcp::socket socket)
            {
                on_accept(ec, std::move(socket));
            });
        }

        void on_accept(error_code_t ec, tcp::socket socket)
        {
            if (!ec)
            {
                auto s = std::make_shared<connection>(*this, std::move(socket));
                connections.insert(s);

                s->run();
            }

            do_accept();
        }

        void dispatch(const request& req, response& rep)
        {
            std::string uri = req.target();

            if (auto p = uri.find_last_of('?'); p != std::string::npos)
                uri = uri.substr(0, p);

            auto it = handlers.find(uri);

            if (it == handlers.end())
            {
                std::cout << uri << " was not found" << std::endl;

                return fail(req, rep, uri + "' was not found.", http::status::not_found);
            }

            it->second(req, rep);
        }

        void register_handler(const std::string& uri, request_handler handler)
        {
            handlers.insert(std::make_pair(uri, handler));
        }

    private:
        net::io_context& ioc;
        tcp::acceptor acceptor;

        connections_t connections;
        request_handlers_t handlers;
    };
}

#endif
