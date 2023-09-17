//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef SEVERITY_LOGGER_HPP
#define SEVERITY_LOGGER_HPP

#include <fstream>
#include <ostream>
#include <boost/log/trivial.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/core/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#define LOGFILE "raft.logs"
#define SEVERITY_THRESHOLD logging::trivial::info

BOOST_LOG_GLOBAL_LOGGER(logger, boost::log::sources::severity_logger_mt<boost::log::trivial::severity_level>)

#define BOOST_GLOBAL_LOG(severity) BOOST_LOG_SEV(logger::get(), boost::log::trivial::severity)

#define LOG_TRACE   BOOST_GLOBAL_LOG(trace)
#define LOG_DEBUG   BOOST_GLOBAL_LOG(debug)
#define LOG_INFO    BOOST_GLOBAL_LOG(info)
#define LOG_WARNING BOOST_GLOBAL_LOG(warning)
#define LOG_ERROR   BOOST_GLOBAL_LOG(error)
#define LOG_FATAL   BOOST_GLOBAL_LOG(fatal)

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

namespace src = boost::log::sources;
namespace expr = boost::log::expressions;

namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

BOOST_LOG_ATTRIBUTE_KEYWORD(line_id, "LineID", unsigned int)
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", logging::trivial::severity_level)

BOOST_LOG_GLOBAL_LOGGER_INIT(logger, src::severity_logger_mt)
{
    src::severity_logger_mt<boost::log::trivial::severity_level> logger;

    logger.add_attribute("LineID", attrs::counter<unsigned int>(1));
    logger.add_attribute("TimeStamp", attrs::local_clock());

    using text_sink = sinks::asynchronous_sink<sinks::text_ostream_backend>;
    boost::shared_ptr<text_sink> sink = boost::make_shared<text_sink>();

    sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(LOGFILE));
    sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));

    logging::formatter formatter = expr::stream << std::setw(7) << std::setfill('0') << line_id
        << std::setfill(' ') << " | " << expr::format_date_time(timestamp, "%Y-%m-%d %H:%M:%S.%f")
        << " " << "[" << logging::trivial::severity << "]" << " - " << expr::smessage;

    sink->set_formatter(formatter);
    sink->set_filter(severity >= SEVERITY_THRESHOLD);

    sink->locked_backend()->auto_flush(false);
    logging::core::get()->add_sink(sink);

    return logger;
}

#endif
