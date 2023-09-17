//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef RAFT_BASE_HPP
#define RAFT_BASE_HPP

#include <chrono>
#include <random>
#include <filesystem>
#include <functional>
#include <arpc.hpp>
#include <severity_logger.hpp>
#include <config_server.hpp>
#include <entry_logger.hpp>
#include <raft.pb.h>

namespace draft
{
    namespace net = boost::asio;

    namespace fs = std::filesystem;
    namespace gp = google::protobuf;

    using Closure = gp::Closure;
    using error_code_t = boost::system::error_code;

    using work = net::io_context::work;
    using work_ptr = std::unique_ptr<work>;

    using log_committed_callback = std::function<void(const log_entry*)>;
    using data_transfer_callback = std::function<void(const std::string&, bool)>;

    using mapped_file_ptr = std::unique_ptr<mapped_file>;
    using entry_logger_ptr = std::unique_ptr<entry_logger>;

    using rpc_server_ptr = std::unique_ptr<arpc::server>;
    using rpc_channel_ptr = std::unique_ptr<arpc::channel>;

    using uniform_int_dist = std::uniform_int_distribution<int>;
    using uniform_int_dist_ptr = std::unique_ptr<uniform_int_dist>;

    using service_ptr = std::unique_ptr<rpc_service>;
    using iterator_t = fs::recursive_directory_iterator;

    using config_server_ptr = std::unique_ptr<config_server>;
    using http_server_ptr = config_server_ptr;

    using http_req = request;
    using http_rep = response;

    enum role_type
    {
        LEADER,
        CANDIDATE,
        FOLLOWER
    };

    enum reconfiguration_type
    {
        server_addition,
        server_removal
    };

    struct vote_state
    {
        uint64_t curr_term;
        char voted_for[32];
    };

    struct node
    {
        char id[32];

        std::string host;
        std::string port;

        bool alive;

        uint64_t next_index;
        uint64_t match_index;

        service_ptr request_vote_service;
        service_ptr append_entries_service;

        service_ptr data_transfer_service;

        bool transfer_leader_to;
        bool transfer_data_to;
    };

    using clock_t = std::chrono::high_resolution_clock;
    using time_point_t = clock_t::time_point;

    template <typename T, typename U>
    struct task_type
    {
        uint64_t id;
        node* n;

        T request;
        U response;

        arpc::controller controller;
        Closure* done;

        time_point_t time_begin;
        time_point_t time_end;
    };

    using vote_task = task_type<vote_req, vote_res>;
    using vote_task_ptr = std::shared_ptr<vote_task>;

    using data_transfer_task = task_type<data_req, data_res>;
    using done_transfer_task = task_type<done_req, data_res>;

    using append_entries_task = task_type<append_entries_req, append_entries_res>;
    using append_entries_task_ptr = std::shared_ptr<append_entries_task>;

    using data_transfer_task_ptr = std::shared_ptr<data_transfer_task>;
    using done_transfer_task_ptr = std::shared_ptr<done_transfer_task>;

    using node_ptr = std::shared_ptr<node>;
    using cluster_t = std::unordered_map<std::string, node_ptr>;

    struct reconfiguration
    {
        reconfiguration_type type;

        reconfiguration(http_rep& rep) : rep(rep)
        {
        }

        std::string address;
        http_rep& rep;

        uint32_t rounds = 0;
        bool entry_added = false;

        node_ptr n;
        time_point_t last_round;
    };

    static constexpr int buff_size = 65536;
    using reconfiguration_ptr = std::shared_ptr<reconfiguration>;

    constexpr int default_min_election_timeout = 150;
    constexpr int default_max_election_timeout = 300;

    constexpr int default_server_catch_up_rounds = 10;
    constexpr int default_election_rpc_timeout = 70;

    constexpr int default_heartbeat_rate = 80;
    constexpr int default_heartbeat_rpc_timeout = 70;

    constexpr int default_snapshot_rate = 120;
    constexpr int default_transfer_rpc_timeout = 0;

    constexpr std::string default_rpc_port = "2023";
    constexpr std::string default_http_port = "2026";
}

#endif
