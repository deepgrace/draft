//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#include <config_server.hpp>

namespace net = boost::asio;

namespace beast = boost::beast;
namespace http = beast::http;

void add_server(const draft::request& req, draft::response& rep)
{
    if (req.method() != http::verb::get && req.method() != http::verb::head)
        return fail(req, rep, "Unknown HTTP method", http::status::bad_request);

    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos)
        return fail(req, rep, "illegal request target", http::status::bad_request);

    std::string body = "OK succeed";
    std::cout << "req.target() " << req.target() << std::endl;

    auto& res = rep.res;

    res.body() = body;
    res.prepare_payload();

    res.result(http::status::ok);
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);

    res.set(http::field::content_type, "application/text");
    res.keep_alive(req.keep_alive());
}

// ./config_server 127.0.0.1 8080

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <host> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 127.0.0.1 8080" << std::endl;

        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];

    net::io_context ioc;

    draft::config_server s(ioc, host, port);
    s.register_handler("/add_server", add_server);

    s.run();
    ioc.run();

    return 0;
}
