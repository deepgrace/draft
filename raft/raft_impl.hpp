//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef RAFT_IMPL_HPP
#define RAFT_IMPL_HPP

#include <array>
#include <atomic>
#include <thread>
#include <concurrent_queue.hpp>
#include <config_parser.hpp>

namespace draft
{
    template <typename R>
    class raft_impl
    {
    public:
        raft_impl(const raft_impl&) = delete;
        raft_impl& operator=(const raft_impl&) = delete;

        friend R;
        using rpc_service_ptr = std::unique_ptr<R>;

        raft_impl(auto&& conf, log_committed_callback lcb, data_transfer_callback dcb) :
        parser(conf), lcb(lcb), dcb(dcb), work_(std::make_unique<work>(ioc)), election_timer(ioc),
        heartbeat_timer(ioc), leadership_transfer_timer(ioc), generator(device())
        {
            request_vote_channel = std::make_unique<arpc::channel>(ioc);

            data_transfer_channel = std::make_unique<arpc::channel>(ioc);
            append_entries_channel = std::make_unique<arpc::channel>(ioc);
        }

        rc_errno load_config()
        {
            std::string config = log->last_config();

            if (!log->cfg_entry_index() && config.empty())
            {
                LOG_INFO << "found no configuration entry, bootstrapping cluster";

                if (auto rc = bootstrap_cluster_config(); rc != RC_GOOD)
                    return rc;
            }
            else
            {
                if (log->cfg_entry_index())
                    config = log->config();

                LOG_INFO << "found configuration entry " << config << ", initing cluster config";

                commit_index_ = log->last_index();
                last_applied_ = log->last_index();

                auto addresses = split(config, ",");

                for (auto& address : addresses)
                {
                     if (!cluster.contains(address))
                     {
                         node_ptr n = make_node(address, address == self_id ? rpc_port_ : default_rpc_port);

                         cluster.emplace(address, n);
                         LOG_INFO << "added server " << address << " to configuration";
                     }
                }
            }

            return RC_GOOD;
        }

        rc_errno bootstrap_cluster_config()
        {
            rc_errno rc;
            std::vector<std::string> servers;

            if (auto p = parser.find("servers"); p != nullptr)
                servers = split(*p, ",");

            if (servers.size())
            {
                std::string address = servers[0];

                if (!is_ipv4(address))
                {
                    LOG_FATAL << "invalid server ip: " << address;

                    return RC_CONF_ERROR;
                }

                make_id(address);

                if (self_id != address)
                {
                    LOG_FATAL << "when bootstrapping, first entry in servers line must be the same as network interface address " << self_id;

                    return RC_CONF_ERROR;
                }

                LOG_INFO << "bootstrap_cluster_config: take " << address << " as the first server in the cluster";

                auto server = make_node(address, rpc_port_);
                cluster.emplace(address, server);

                auto entry = make_entry(serialize_configuration(), log->last_entry_index() + 1, 1, true);

                if (rc = log->append(entry, true); rc != RC_GOOD)
                {
                    LOG_ERROR << "bootstrap_cluster_config: failed to append configuration entry to log";

                    return rc;
                }

                server->next_index = log->last_entry_index() + 1;
                server->match_index = log->last_entry_index();

                commit_index_ = entry->index;
            }
            else
                LOG_INFO << "no servers line found, started as a non-voting server";

            LOG_INFO << "bootstrap_cluster_config: finished bootstrapping";

            return RC_GOOD;
        }

        rc_errno init()
        {
            if (inited_)
                return RC_GOOD;

            auto rc = parser.parse();

            if (rc != RC_GOOD)
            {
                LOG_FATAL << "failed in parse of configuration";

                return rc;
            }

            std::string* path = parser.find("path");

            if (path == nullptr)
            {
                LOG_FATAL << "must specify path in conf file";

                return RC_CONF_ERROR;
            }

            set_path(*path);
            init_transfer();

            std::string* vote_file = parser.find("vote_file");

            if (vote_file == nullptr)
            {
                LOG_FATAL << "must specify vote_file in conf file";

                return RC_CONF_ERROR;
            }

            vote_data = std::make_unique<mapped_file>(*vote_file, sizeof(vote_state));

            if (rc = vote_data->map(); rc != RC_GOOD && rc != RC_MMAP_NEW_FILE)
            {
                LOG_FATAL << "vote_data->map() failed";

                return rc;
            }

            sync_all();

            std::string* stat_file = parser.find("stat_file");

            if (stat_file == nullptr)
            {
                LOG_FATAL << "must specify stat_file in conf file";

                return RC_CONF_ERROR;
            }

            std::string* ldbs_file = parser.find("ldbs_file");

            if (ldbs_file == nullptr)
            {
                LOG_FATAL << "must specify ldbs_file in conf file";

                return RC_CONF_ERROR;
            }

            log = std::make_unique<entry_logger>(*stat_file, *ldbs_file);

            if (rc = log->init(); rc != RC_GOOD)
            {
                LOG_FATAL << "log->init() failed";

                return rc;
            }

            std::string* interface = parser.find("interface");

            if (interface == nullptr)
            {
                LOG_INFO << "missing interface, lo is used as default";
                self_id = interface_address = get_ipv4("lo");
            }
            else
                self_id = interface_address = get_ipv4(*interface);

            if (self_id.empty() || !is_ipv4(self_id))
            {
                LOG_FATAL << "invalid ip address " << self_id << " of interface " << interface ? *interface : "lo";

                return RC_CONF_ERROR;
            }

            LOG_INFO << "interface " << (interface ? *interface : "lo") << "'s address: " << interface_address;

            std::string* min_election_timeout = parser.find("min_election_timeout");

            if (min_election_timeout == nullptr)
            {
                LOG_INFO << "missing min_election_timeout, " << default_min_election_timeout << "ms is used as default";
                min_election_timeout_ = default_min_election_timeout;
            }
            else
                min_election_timeout_ = std::stoi(*min_election_timeout);

            std::string* max_election_timeout = parser.find("max_election_timeout");

            if (max_election_timeout == nullptr)
            {
                LOG_INFO << "missing max_election_timeout, " << default_max_election_timeout << "ms is used as default";
                max_election_timeout_ = default_max_election_timeout;
            }
            else
                max_election_timeout_ = std::stoi(*max_election_timeout);

            dist = std::make_unique<uniform_int_dist>(min_election_timeout_, max_election_timeout_);

            std::string* server_catch_up_rounds = parser.find("server_catch_up_rounds");

            if (server_catch_up_rounds == nullptr)
            {
                LOG_INFO << "missing server_catch_up_rounds, " << default_server_catch_up_rounds << " rounds is used as default";
                server_catch_up_rounds_ = default_server_catch_up_rounds;
            }
            else
                server_catch_up_rounds_ = std::stoi(*server_catch_up_rounds);

            std::string* election_rpc_timeout = parser.find("election_rpc_timeout");

            if (election_rpc_timeout == nullptr)
            {
                LOG_INFO << "missing election_rpc_timeout, " << default_election_rpc_timeout << "ms is used as default";
                election_rpc_timeout_ = default_election_rpc_timeout;
            }
            else
                election_rpc_timeout_ = std::stoi(*election_rpc_timeout);

            std::string* heartbeat_rate = parser.find("heartbeat_rate");

            if (heartbeat_rate == nullptr)
            {
                LOG_INFO << "missing heartbeat_rate, " << default_heartbeat_rate << "ms is used as default";
                heartbeat_rate_ = default_heartbeat_rate;
            }
            else
                heartbeat_rate_ = std::stoi(*heartbeat_rate);

            std::string* heartbeat_rpc_timeout = parser.find("heartbeat_rpc_timeout");

            if (heartbeat_rpc_timeout == nullptr)
            {
                LOG_INFO << "missing heartbeat_rpc_timeout, " << default_heartbeat_rpc_timeout << "ms is used as default";
                heartbeat_rpc_timeout_ = default_heartbeat_rpc_timeout;
            }
            else
                heartbeat_rpc_timeout_ = std::stoi(*heartbeat_rpc_timeout);

            std::string* transfer_rpc_timeout = parser.find("transfer_rpc_timeout");

            if (transfer_rpc_timeout == nullptr)
            {
                LOG_INFO << "missing transfer_rpc_timeout, " << default_transfer_rpc_timeout << "ms is used as default";
                transfer_rpc_timeout_ = default_transfer_rpc_timeout;
            }
            else
                transfer_rpc_timeout_ = std::stoi(*transfer_rpc_timeout);

            std::string* snapshot_rate = parser.find("snapshot_rate");

            if (snapshot_rate == nullptr)
            {
                LOG_INFO << "missing snapshot_rate, " << default_snapshot_rate << "seconds is used as default";
                snapshot_rate_ = default_snapshot_rate;
            }
            else
                snapshot_rate_ = std::stoi(*snapshot_rate);

            std::string* log_compaction_enabled = parser.find("log_compaction_enabled");

            if (log_compaction_enabled == nullptr)
            {
                LOG_INFO << "missing log_compaction_enabled, true is used as default";
                log_compaction_enabled_ = true;
            }
            else
            {
                auto& str = *log_compaction_enabled;

                if (str == "true" || str == "1")
                    log_compaction_enabled_ = true;
                else if (str == "false" || str == "0")
                    log_compaction_enabled_ = false;
            }

            std::string* rpc_host = parser.find("rpc_host");

            if (rpc_host == nullptr)
                LOG_INFO << "missing rpc_host, ADDR_ANY is used as default";
            else
                rpc_host_ = *rpc_host;

            std::string* rpc_port = parser.find("rpc_port");

            if (rpc_port == nullptr)
            {
                LOG_INFO << "missing rpc_port, " << default_rpc_port << " is used as default";
                rpc_port_ = default_rpc_port;
            }
            else if (auto p = std::stoi(*rpc_port); (p < 1024 || p > 65535))
            {
                LOG_FATAL << "rpc_port out of range [1024-65535]";

                return RC_CONF_ERROR;
            }
            else
                rpc_port_ = *rpc_port;

            make_id(self_id);

            if (rpc_host_.empty())
                rpc_server = std::make_unique<arpc::server>(ioc, rpc_port_);
            else
                rpc_server = std::make_unique<arpc::server>(ioc, rpc_host_, rpc_port_);

            rpc_service_ = std::make_unique<R>(this);

            done_ = gp::NewPermanentCallback(rpc_service_.get(), &R::done);
            rpc_server->register_service(rpc_service_.get(), done_);

            if (rc = load_config(); rc != RC_GOOD)
                return rc;

            std::string* http_host = parser.find("http_host");

            if (http_host == nullptr)
                LOG_INFO << "missing http_host, ADDR_ANY is used as default";
            else
                http_host_ = *http_host;

            std::string* http_port = parser.find("http_port");

            if (http_port == nullptr)
            {
                LOG_INFO << "missing http_port, " << default_http_port << " is used as default";
                http_port_ = default_http_port;
            }
            else if (auto p = std::stoi(*http_port); (p < 1024 || p > 65535))
            {
                LOG_FATAL << "http_port out of range [1024-65535]";

                return RC_CONF_ERROR;
            }
            else
                http_port_ = *http_port;

            if (http_host_.empty())
                http_server = std::make_unique<config_server>(ioc, http_port_);
            else
                http_server = std::make_unique<config_server>(ioc, http_host_, http_port_);

            register_http_service();

            inited_ = true;

            return RC_GOOD;
        }

        rc_errno start()
        {
            if (!inited_)
            {
                LOG_WARNING << "raft's not been initialized";

                return RC_ERROR;
            }

            if (started_)
            {
                LOG_WARNING << "raft's already been started";

                return RC_GOOD;
            }

            rpc_server->run();
            http_server->run();

            started_at = clock_t::now();
            last_heartbeat = time_point_t::min();

            if (cluster.size())
            {
                LOG_DEBUG << "resetting election timer by start";
                reset_election_timer();
            }

            log_committed_thread = std::thread([this]{ apply_log_committed(); });
            data_transfer_thread = std::thread([this]{ apply_data_transfer(); });

            started_ = true;
            work_.reset(new net::io_service::work(ioc));

            while (started_)
            {
                try
                {
                    ioc.run();
                }
                catch(std::exception& e)
                {
                    LOG_ERROR << e.what();
                }
            }

            return RC_GOOD;
        }

        rc_errno stop()
        {
            if (started_)
            {
                started_ = false;

                log_committed_queue.push(true);
                data_transfer_queue.push(true);

                work_.reset();

                rpc_server->stop();
                http_server->stop();

                ioc.stop();

                log_committed_thread.join();
                data_transfer_thread.join();
            }

            return RC_GOOD;
        }

        log_committed_callback log_committed_cb()
        {
            return lcb;
        }

        void log_committed_cb(const log_committed_callback& cb)
        {
            lcb = cb;
        }

        data_transfer_callback data_transfer_cb()
        {
            return dcb;
        }

        void data_transfer_cb(const data_transfer_callback& cb)
        {
            dcb = cb;
        }

        role_type role()
        {
            return role_;
        }

        std::string role_string()
        {
            return role_ == FOLLOWER ? "Follower" : (role_ == CANDIDATE ? "Candidate" : "Leader");
        }

        fs::path& path()
        {
            return path_;
        }

        fs::path& parent()
        {
            return parent_;
        }

        void set_path(const fs::path& path)
        {
            path_ = path;
            parent_ = path_.parent_path();
        }

        uint32_t snapshot_rate()
        {
            return snapshot_rate_;
        }

        void set_snapshot_rate(uint32_t n)
        {
            snapshot_rate_ = n;
        }

        uint32_t log_compaction_enabled()
        {
            return log_compaction_enabled_;
        }

        void set_log_compaction_enabled(bool b)
        {
            log_compaction_enabled_ = b;
        }

        std::string relative(const fs::path& path)
        {
            if (parent_.empty())
                return path;

            return fs::relative(path, parent_);
        }

        template <typename T>
        void set_perms(T& t, const fs::path& path)
        {
            fs::file_status s = fs::status(path);
            t.set_perms(std::to_underlying(s.permissions()));
        }

        template <typename T>
        void set_last(T& t, const fs::path& path)
        {
            set_perms(t, path);
            last = relative(path);

            LOG_INFO << "transferring " << last;
        }

        void init_transfer()
        {
            data_transfer_id = 0;
            done_transfer_id = 0;

            auto value = !fs::is_directory(path_);
            range = value ? iterator_t() : iterator_t(path_);

            begin = value ? iterator_t() : fs::begin(range);
            end = value ? iterator_t() : fs::end(range);
        }

        vote_state* vote_info()
        {
            return static_cast<vote_state*>(vote_data->addr());
        }

        void sync_all()
        {
            vote_data->sync_all();
        }

        uint64_t& curr_term()
        {
            return vote_info()->curr_term;
        }

        char* voted_for()
        {
            return vote_info()->voted_for;
        }

        size_t voted_for_size()
        {
            return sizeof(vote_info()->voted_for);
        }

        std::string& leader()
        {
            return leader_;
        }

        std::string leader_host()
        {
            return leader_.substr(0, leader_.find(":"));
        }

        std::string leader_port()
        {
            return leader_.substr(leader_.find(":") + 1);
        }

        uint64_t log_size()
        {
            return log->payload();
        }

        std::atomic<uint64_t>& commit_index()
        {
            return commit_index_;
        }

        std::atomic<uint64_t>& last_applied()
        {
            return last_applied_;
        }

        rc_errno drop(uint64_t index, bool sync = false)
        {
            std::unique_lock lock(mutex_);

            return log->drop(index, sync);
        }

        log_entry_ptr make_entry(const std::string& data, bool cfg = false)
        {
            return make_entry(data, log->last_entry_index() + 1, curr_term(), cfg);
        }

        log_entry_ptr make_entry(const std::string& data, uint64_t index, uint64_t term, bool cfg)
        {
            auto entry = make_log_entry(data.size(), cfg);

            entry->index = index;
            entry->term = term;

            entry->cfg = cfg;
            log->copy(entry.get(), data, cfg);

            return entry;
        }

        uint64_t append(const std::string& data)
        {
            if (role_ != LEADER)
                return RC_NOT_LEADER;

            log_entry_ptr entry = make_entry(data);
            LOG_INFO << "appending entry: " << to_string(entry);

            if (log->append(entry, true) != RC_GOOD)
                return 0;

            return entry->index;
        }

    private:
        template <typename S, typename C, typename... Args, typename T>
        void invoke(S& s, void (C::*m)(Args...), T t)
        {
            (s.get()->*m)(&t->controller, &t->request, &t->response, t->done);
        }

        template <typename T>
        void fill(T task, node* n, uint64_t id, uint32_t timeout, void (raft_impl::*m)(T), bool early = false)
        {
            task->n = n;
            task->id = id;

            auto& req = task->request;
            auto& ctl = task->controller;

            ctl.host(n->host);
            ctl.port(n->port);

            ctl.timeout(timeout);
            req.set_term(curr_term());

            using U = std::remove_cvref_t<decltype(req)>;

            if constexpr(requires { std::declval<U>().set_leader_id(""); })
                req.set_leader_id(self_id);

            if constexpr(requires { std::declval<U>().set_candidate_id(""); })
                req.set_candidate_id(self_id);

            if constexpr(requires { std::declval<U>().set_last_index(0); })
            {
                if (log_compaction_enabled())
                    req.set_last_index(log->last_index());
            }

            if constexpr(requires { std::declval<U>().set_last_term(0); })
            {
                if (log_compaction_enabled())
                    req.set_last_term(log->last_term());
            }

            if constexpr(requires { std::declval<U>().set_last_config(""); })
            {
                if (log_compaction_enabled() && id == 1)
                    req.set_last_config(log->last_config());
            }

            if constexpr(requires { std::declval<U>().set_prev_log_index(0); std::declval<U>().set_prev_log_term(0); })
            {
                log_entry* prev_log = (*log)[n->next_index - 1];

                req.set_prev_log_index(prev_log != nullptr ? prev_log->index : log->last_index());
                req.set_prev_log_term(prev_log != nullptr ? prev_log->term : log->last_term());
            }

            if constexpr(requires { std::declval<U>().set_last_log_index(0); })
                req.set_last_log_index(log->last_entry_index());

            if constexpr(requires { std::declval<U>().set_last_log_term(0); })
                req.set_last_log_term(log->last_entry_term());

            if constexpr(requires { std::declval<U>().set_leader_commit(0); })
                req.set_leader_commit(commit_index_);

            if constexpr(requires { std::declval<U>().set_early(0); })
                req.set_early(early);

            if constexpr(requires { std::declval<U>().set_id(0); })
                req.set_id(task->id);

            task->done = gp::NewCallback(this, m, task);
            task->time_begin = clock_t::now();
        }

        void pre_vote(bool early = false)
        {
            bool exist = cluster.contains(self_id);

            if (cluster.size() == 1 && exist)
            {
                leader_election();

                return;
            }

            pre_votes_granted = exist;
            pre_votes_refused = 0;

            role_ = FOLLOWER;
            LOG_INFO << "election timer timed out, start pre_vote phase";

            for (auto& [_, n] : cluster)
            {
                 if (n->id == self_id)
                     continue;

                 auto task = std::make_shared<vote_task>();
                 fill(task, n.get(), ++pre_vote_id, election_rpc_timeout_, &raft_impl::on_pre_vote, early);

                 invoke(n->request_vote_service, &rpc_service::pre_vote, task);
            }
        }

        void on_pre_vote(vote_task_ptr task)
        {
            auto& ctl = task->controller;
            auto& res = task->response;

            task->time_end = clock_t::now();

            if (role_ == CANDIDATE || role_ == LEADER)
            {
                LOG_INFO << "pre_vote " << task->id << ": election already started or elected as leader, ignoring stale response";

                return;
            }

            if (ctl.Failed())
            {
                ++pre_votes_refused;
                checkout();

                return;
            }

            if (res.vote_granted())
                ++pre_votes_granted;
            else
                ++pre_votes_refused;

            checkout();
        }

        void checkout()
        {
            if (pre_votes_granted > cluster.size() / 2)
            {
                LOG_INFO << "pre_vote phase succeed, initiating election";
                leader_election();
            }
            else if (pre_votes_granted + pre_votes_refused == cluster.size())
            {
                LOG_INFO << "pre_vote phase failed, quit initiating election, restart election timer";
                reset_election_timer();
            }
        }

        void request_vote(node* n)
        {
            auto task = std::make_shared<vote_task>();
            fill(task, n, ++request_vote_id, election_rpc_timeout_, &raft_impl::on_request_vote);

            invoke(n->request_vote_service, &rpc_service::request_vote, task);
        }

        void on_request_vote(vote_task_ptr task)
        {
            auto& ctl = task->controller;

            auto& req = task->request;
            auto& res = task->response;

            task->time_end = clock_t::now();

            if (ctl.Failed())
                return;

            if (role_ != CANDIDATE)
            {
                LOG_INFO << "request_vote " << task->id << ": current role is not candidate, ignoring stale response";

                return;
            }

            if (res.vote_granted())
            {
                if (res.term() < curr_term())
                    return;

                LOG_INFO << "request_vote " << task->id << " at term " << req.term() << " got granted from " << ctl.host() << ":" << ctl.port();

                if (++votes > cluster.size() / 2)
                    step_up();
            }
            else if (res.term() > curr_term())
            {
                LOG_INFO << "calling step_down from on_request_vote";
                step_down(res.term(), "");
            }
        }

        void leader_election()
        {
            role_ = CANDIDATE;
            leader_ = "unknown";

            ++curr_term();
            std::memcpy(voted_for(), self_id.c_str(), voted_for_size());

            sync_all();
            votes = cluster.contains(self_id);

            LOG_INFO << "start a new election at term " << curr_term();

            if (cluster.size() == 1 && votes)
            {
                step_up();

                return;
            }

            for (auto& [_, n] : cluster)
                 if (n->id != self_id)
                     request_vote(n.get());

            reset_election_timer();
        }

        void remove_election_timer()
        {
            error_code_t ec;
            election_timer.cancel(ec);

            if (ec)
                LOG_ERROR << "election_timer.cancel error: " << ec.message();
        }

        void reset_election_timer()
        {
            uint32_t timeout = (*dist.get())(generator);

            election_timer.expires_from_now(boost::posix_time::milliseconds(timeout));

            election_timer.async_wait([this](const error_code_t& ec)
            {
                on_election_timeout(ec);
            });
        }

        void on_election_timeout(const error_code_t& ec)
        {
            if (!ec)
                pre_vote();
            else if (ec.value() != boost::system::errc::operation_canceled)
                LOG_ERROR << "on_election_timeout error: " << ec.message();
            else
                LOG_TRACE << "election timer canceled";
        }

        void append_entries()
        {
            reset_heartbeat_timer();

            if (role_ != LEADER)
                return;

            last_heartbeat = clock_t::now();

            for (auto& [_, n]: cluster)
                 if (n->id != self_id && !n->transfer_data_to)
                     append_entries(n.get());

            if (cluster.size() == 1 && cluster.contains(self_id))
                adjust_commit_index();
        }

        void append_entries(node* n)
        {
            auto task = std::make_shared<append_entries_task>();
            auto& req = task->request;

            for (uint64_t i = n->next_index; i <= log->last_entry_index(); ++i)
            {
                 log_entry* l = (*log)[i];

                 if (l == nullptr)
                 {
                     auto peer = n->host + ":" + n->port;
                     auto tail = log->last_entry_index();

                     LOG_INFO << peer << " left behind log entries with index range [" << i << ", " << tail + 1 << ")";

                     break;
                 }

                 LogEntry* e = req.add_entries();

                 e->set_term(l->term);
                 e->set_index(l->index);

                 e->set_data(l->str());
                 e->set_config(l->cfg);
            }

            fill(task, n, ++append_entries_id, heartbeat_rpc_timeout_, &raft_impl::on_append_entries);

            task->time_begin = clock_t::now();
            invoke(n->append_entries_service, &rpc_service::append_entries, task);
        }

        void on_append_entries(append_entries_task_ptr task)
        {
            auto& ctl = task->controller;
            auto& res = task->response;

            node* n = task->n;
            task->time_end = clock_t::now();

            if (ctl.Failed())
            {
                if (newly(n))
                    handle_catch_up_server_append_entries();

                return;
            }

            if (role_ != LEADER)
                return;

            if (res.success())
            {
                n->match_index = res.match_index();
                n->next_index = n->match_index + 1;

                adjust_commit_index();

                if (n->transfer_leader_to)
                    handle_transfer_leader_to(n);

                if (newly(n))
                    handle_catch_up_server_append_entries();
            }
            else if (res.term() > curr_term())
            {
                LOG_INFO << "calling step_down from on_append_entries";
                step_down(res.term(), n->id);
            }
            else
            {
                n->next_index = res.match_index() + 1;
                LOG_INFO << "append_entries " << task->id << ": log inconsistency, retrying with next index " << n->next_index;

                if ((*log)[n->next_index] == nullptr)
                {
                    LOG_INFO << "next log entry responsed by peer at index " << n->next_index << " doesn't exist, start data transfer";

                    init_transfer();
                    n->transfer_data_to = true;

                    do_data_transfer(n);
                }
                else
                    append_entries(n);
            }
        }

        void transfer(data_req& req)
        {
            auto path = begin->path();

            while (fs::is_directory(path) && !fs::is_empty(path))
            {
                ++begin;
                path = begin->path();
            }

            if (fs::is_directory(path))
            {
                set_last(req, path);
                req.set_name(last + "/");
            }
            else if (fs::is_regular_file(path))
                transfer(req, path);
        }

        void transfer(data_req& req, const fs::path& path)
        {
            if (!fs::file_size(path))
                set_last(req, path);
            else
            {
                if (!fin.is_open())
                {
                    set_last(req, path);
                    fin.open(path, std::ios_base::in | std::ios_base::binary);
                }

                fin.read(buff.data(), buff.size());
                req.set_data(buff.data(), fin.gcount());
            }

            req.set_name(last);
        }

        void do_data_transfer(const std::string& address, const fs::path& path)
        {
            if (role_ != LEADER)
                return;

            auto it = cluster.find(address);

            if (it == cluster.end())
                return;

            set_path(path);
            init_transfer();

            do_data_transfer(it->second.get());
        }

        void do_data_transfer(node* n)
        {
            if (role_ != LEADER)
                return;

            if (begin == end)
            {
                if (newly(n) && !fs::exists(path_) && !n->transfer_data_to)
                {
                    LOG_INFO << "start catching up phase for server " << n->id;
                    LOG_INFO << "start round " << recfg->rounds << " of replication to server " << n->id;

                    append_entries(n);

                    return;
                }
            }

            auto task = std::make_shared<data_transfer_task>();
            fill(task, n, ++data_transfer_id, transfer_rpc_timeout_, &raft_impl::on_data_transfer);

            auto& req = task->request;

            if (data_transfer_id == 1)
            {
                n->transfer_data_to = true;
                n->next_index = log->last_entry_index() + 1;
            }

            if (begin != end)
                transfer(req);
            else if (fs::is_regular_file(path_))
                transfer(req, path_);
            else if (fs::is_directory(path_))
            {
                set_last(req, path_);
                req.set_name(last + "/");
            }

            task->time_begin = clock_t::now();
            invoke(n->data_transfer_service, &rpc_service::data_transfer, task);
        }

        void on_data_transfer(data_transfer_task_ptr task)
        {
            auto& req = task->request;
            auto& res = task->response;

            node* n = task->n;
            auto& ctl = task->controller;

            task->time_end = clock_t::now();

            if (ctl.Failed())
            {
                LOG_INFO << "data_transfer " << task->id << ": failed to transfer data to server " << n->id
                         << ", error code: " << ctl.ErrorCode() << ", error text: " << ctl.ErrorText();

                if (newly(n))
                    do_data_transfer(n);

                return;
            }

            if (begin != end)
            {
                auto path = begin->path();

                if (fs::is_directory(path))
                    ++begin;
                else if (fs::is_regular_file(path) && eof(path))
                {
                    ++begin;
                    fin.close();
                }
            }

            if (res.success())
            {
                if (begin == end)
                {
                    if (fs::is_regular_file(path_))
                    {
                        if (eof(path_))
                            fin.close();
                        else
                            return do_data_transfer(n);
                    }

                    do_done_transfer(n);
                }
                else
                    do_data_transfer(n);
            }
            else if (res.term() > curr_term())
            {
                LOG_INFO << "calling step_down from on_data_transfer";
                step_down(res.term(), n->id);
            }
            else
                LOG_INFO << "data_transfer to " << req.id() << " failed";
        }

        void do_done_transfer(node* n)
        {
            auto task = std::make_shared<done_transfer_task>();
            fill(task, n, ++done_transfer_id, transfer_rpc_timeout_, &raft_impl::on_done_transfer);

            task->request.set_name(last);
            invoke(n->data_transfer_service, &rpc_service::done_transfer, task);
        }

        void on_done_transfer(done_transfer_task_ptr task)
        {
            auto& req = task->request;
            auto& res = task->response;

            node* n = task->n;
            auto& ctl = task->controller;

            task->time_end = clock_t::now();

            if (ctl.Failed())
            {
                LOG_INFO << "done_transfer " << task->id << ": failed to transfer done to server " << n->id
                         << ", error code: " << ctl.ErrorCode() << ", error text: " << ctl.ErrorText();

                if (newly(n))
                    do_done_transfer(n);

                return;
            }

            if (res.success())
            {
                LOG_INFO << "transfer done";

                init_transfer();
                n->transfer_data_to = false;

                if (newly(n))
                {
                    LOG_INFO << "start catching up phase for server " << n->id;
                    LOG_INFO << "start round " << recfg->rounds << " of replication to server " << n->id;
                }

                append_entries(n);
            }
            else if (res.term() > curr_term())
            {
                LOG_INFO << "calling step_down from on_done_transfer";
                step_down(res.term(), n->id);
            }
            else
                LOG_INFO << "done_transfer to " << req.id() << " failed";
        }

        bool eof(const std::string& file)
        {
            return fin.gcount() != buff_size || fs::file_size(file) == buff_size;
        }

        bool newly(node* n)
        {
            return recfg.get() && recfg->n.get() == n && recfg->type == server_addition;
        }

        void remove_heartbeat_timer()
        {
            error_code_t ec;
            heartbeat_timer.cancel(ec);

            if (ec)
                LOG_ERROR << "election_timer.cancel error: " << ec.message();
        }

        void reset_heartbeat_timer()
        {
            heartbeat_timer.expires_from_now(boost::posix_time::milliseconds(heartbeat_rate_));

            heartbeat_timer.async_wait([this](const error_code_t& ec)
            {
                on_heartbeat_timeout(ec);
            });
        }

        void on_heartbeat_timeout(const error_code_t& ec)
        {
            if (!ec)
                append_entries();
            else if (ec.value() != boost::system::errc::operation_canceled)
                LOG_DEBUG << "on_heartbeat_timeout error: " << ec.message();
            else
                LOG_DEBUG << "heartbeat check timer canceled";
        }

        void step_up()
        {
            role_ = LEADER;
            leader_ = self_id;

            LOG_INFO << "now i'm the leader " << self_id << " at term " << curr_term() << ", sending out heartbeats";

            for (auto& [_, n] : cluster)
            {
                 n->match_index = 0;
                 n->next_index = log->last_entry_index() + 1;
            }

            remove_election_timer();
            append_entries();
        }

        void step_down(uint64_t new_term, const std::string& cur_leader)
        {
            LOG_INFO << "stepped down at term " << curr_term() << " to new term " << new_term;
            remove_heartbeat_timer();

            role_ = FOLLOWER;
            curr_term() = new_term;

            sync_all();

            LOG_DEBUG << "resetting election timer by step_down";
            reset_election_timer();

            if (server_transfer_leader_to)
            {
                LOG_INFO << "leadership successfully transferred to " << server_transfer_leader_to->id;
                server_transfer_leader_to->transfer_leader_to = false;

                if (recfg.get() && recfg->type == server_removal)
                {
                    leader_ = cur_leader;
                    complete_reconfiguration("OK");

                    LOG_INFO << "recfg.reset() step_down, current leader now is " << leader_;

                    error_code_t ec;
                    leadership_transfer_timer.cancel(ec);
                }
                else if (server_transfer_reply_to && !server_transfer_reply_to->ready)
                {
                    auto& rep = *server_transfer_reply_to;
                    prepare(rep, "OK " + std::string(server_transfer_leader_to->id));

                    rep.do_write();

                    error_code_t ec;
                    leadership_transfer_timer.cancel(ec);
                }

                server_transfer_reply_to = nullptr;
                server_transfer_leader_to = nullptr;
            }
        }

        void adjust_commit_index()
        {
            uint64_t min_match = 0;
            uint64_t max_match = 0;

            {
                std::unique_lock lock(mutex_);
                min_match = log->last_entry_index();
            }

            uint32_t old_commit = commit_index_;

            for (auto& [_, n] : cluster)
            {
                 if (n->id != self_id && n->match_index > commit_index_ && (*log)[n->match_index]->term == curr_term())
                 {
                     min_match = std::min(min_match, n->match_index);
                     max_match = std::max(max_match, n->match_index);
                 }
            }

            for (uint64_t i = max_match; i >= min_match; --i)
            {
                 uint32_t count = cluster.contains(self_id);

                 for (auto& [_, n] : cluster)
                      if (n->id != self_id && n->match_index >= i)
                          ++count;

                 if (count > cluster.size() / 2)
                 {
                     commit_index_ = i;
                     log_committed_queue.push(false);

                     break;
                 }
            }

            if (cluster.size() == 1 && cluster.contains(self_id))
            {
                commit_index_ = log->last_entry_index();

                if (old_commit < commit_index_)
                    log_committed_queue.push(false);
            }

            uint64_t index = log->cfg_entry_index();

            if (old_commit < index && index <= commit_index_ && recfg.get())
            {
                auto n = recfg->n;

                if (recfg->type == server_addition)
                {
                    n->alive = true;
                    LOG_INFO << "successfully added the server " << n->id << " to the cluster";
                }
                else if (recfg->type == server_removal)
                {
                    n->alive = false;
                    LOG_INFO << "successfully removed the server " << n->id << " from the cluster";
                }

                complete_reconfiguration("OK");
                LOG_INFO << "recfg.reset() adjust_commit_index";
            }
        }

        std::string serialize_configuration()
        {
            size_t i = 0;
            std::string config;

            for (auto& [_, n] : cluster)
            {
                 config.append(n->id);

                 if (i++ != cluster.size() - 1)
                     config.append(",");
            }

            LOG_INFO << "serialized config: " << config;

            return config;
        }

        void make_id(std::string& address)
        {
            std::vector<std::string> v = split(address, ":");

            if (v.size() == 1)
            {
                auto port = address == interface_address ? rpc_port_ : default_rpc_port;
                address.append(":").append(port);
            }
        }

        node_ptr make_node(const std::string& address, const std::string& port)
        {
            auto n = std::make_shared<node>();
            std::vector<std::string> v = split(address, ":");

            if (v.size() == 1)
            {
                n->host = address;
                n->port = port;

                LOG_INFO << "make_node: missing rpc port for server " << address << ", " << port << " is used as default";
            }
            else
            {
                n->host = v[0];
                n->port = v[1];
            }

            std::memcpy(n->id, (n->host + ":" + n->port).c_str(), sizeof(n->id));

            n->alive = false;

            n->match_index = 0;
            n->next_index = log->last_entry_index() + 1;

            n->transfer_leader_to = false;
            n->transfer_data_to = false;

            n->request_vote_service = service_ptr(new rpc_service::Stub(request_vote_channel.get()));

            n->data_transfer_service = service_ptr(new rpc_service::Stub(data_transfer_channel.get()));
            n->append_entries_service = service_ptr(new rpc_service::Stub(append_entries_channel.get()));

            return n;
        }

        std::string latest_server()
        {
            node_ptr latest = nullptr;

            for (auto& [_, n] : cluster)
                 if (n->id != self_id && (latest == nullptr || latest->match_index < n->match_index))
                     latest = n;

            return latest != nullptr ? latest->id : self_id;
        }

        void handle_catch_up_server_append_entries()
        {
            node_ptr n = recfg->n;
            rc_errno rc = RC_NOT_LEADER;

            auto now = clock_t::now();
            uint64_t msecs = std::chrono::duration_cast<std::chrono::milliseconds>(now - recfg->last_round).count();

            if (recfg->rounds < server_catch_up_rounds_ - 1)
            {
                ++recfg->rounds;
                recfg->last_round = clock_t::now();

                LOG_INFO << "start round " << recfg->rounds << " of replication to server " << n->id;
                append_entries(n.get());
            }
            else if (msecs < min_election_timeout_ && log->last_entry_index() - n->match_index <= 5)
            { 
                LOG_INFO << "after " << server_catch_up_rounds_ << " rounds of replication, "
                         << "the new server " << n->id << " caught up with leader, deploying the reconfiguration";

                if (!recfg->entry_added)
                {
                    cluster.insert(std::make_pair(n->id, n));
                    log_entry_ptr cfg_entry = make_entry(serialize_configuration(), true);

                    if (log->append(cfg_entry, true) != RC_GOOD)
                    {
                        LOG_ERROR << "append_entries: failed to append configuration entry to log, error code: " << rc;

                        complete_reconfiguration("ERROR");
                        LOG_INFO << "recfg.reset() handle_catch_up_server_append_entries";

                        return;
                    }

                    recfg->entry_added = true;
                }

                append_entries();
            }
            else if (recfg->rounds >= server_catch_up_rounds_ - 1)
            {
                LOG_INFO << "after " << server_catch_up_rounds_ << " rounds of replication, " << "the new server "
                         << n->id << " is still " << (int)((1 - (double)n->match_index / log->last_entry_index()) * 100)
                         << "% behind the leader, abort the reconfiguration";

                complete_reconfiguration("TIMEOUT");
                LOG_INFO << "recfg.reset() handle_catch_up_server_append_entries";
            }
        }

        void complete_reconfiguration(const std::string& status)
        {
            auto& rep = recfg->rep;
            prepare(rep, status + " " + leader_);

            rep.res.result(http::status::ok);
            rep.do_write();

            recfg.reset();
        }

        void make_config(const std::string& address, node_ptr n, http_rep& rep, reconfiguration_type type)
        {
            rep.ready = false;
            recfg = std::make_shared<reconfiguration>(rep);

            recfg->address = address;
            recfg->type = type;

            recfg->n = n;
        }

        rc_errno add_server(std::string address, http_rep& rep)
        {
            make_id(address);

            if (role_ != LEADER)
            {
                LOG_INFO << "add_server: current role is not leader, ignoring";
                prepare(rep, "NOT_LEADER " + leader_);

                return RC_ERROR;
            }
            else if (cluster.contains(address))
            {
                LOG_INFO << "add_server: server " << address << " already in the configuration, ignoring";
                prepare(rep, "IN_CFG " + leader_);

                return RC_ERROR;
            }
            else if (!is_ipv4(address))
            {
                LOG_INFO << "add_server: invalid server address " << address << ", ignoring";
                prepare(rep, "INVALID_SERVER " + leader_);

                return RC_ERROR;
            }
            else if (recfg.get())
            {
                LOG_INFO << "add_server: reconfiguration of server "<< recfg->n->id << " is in progress";
                prepare(rep, "INPROGRESS " + leader_);

                return RC_ERROR;
            }

            node_ptr n = make_node(address, default_rpc_port);
            make_config(address, n, rep, server_addition);

            recfg->last_round = clock_t::now();

            if (log_compaction_enabled())
            {
                init_transfer();
                do_data_transfer(n.get());
            }
            else
            {
                LOG_INFO << "start catching up phase for server " << n->id;
                LOG_INFO << "start round " << recfg->rounds << " of replication to server " << n->id;

                append_entries(n.get());
            }

            return RC_GOOD;
        }

        rc_errno del_server(std::string address, http_rep& rep)
        {
            make_id(address);

            if (role_ != LEADER)
            {
                LOG_INFO << "del_server: current role is not leader, ignoring";
                prepare(rep, "NOT_LEADER " + leader_);

                return RC_ERROR;
            }
            else if (!cluster.contains(address))
            {
                LOG_INFO << "del_server: server " << address << " not in the configuration, ignoring";
                prepare(rep, "NOT_IN_CFG " + leader_);

                return RC_ERROR;
            }
            else if (recfg.get())
            {
                LOG_INFO << "del_server: reconfiguration of server "<< recfg->n->id << " is in progress";
                prepare(rep, "INPROGRESS " + leader_);

                return RC_ERROR;
            }

            if (cluster.size() == 1)
            {
                LOG_INFO << "del_server: this is the unique server in the cluster "<< leader_ << " can't be removed";
                prepare(rep, "UNIQUE_SERVER " + leader_);

                return RC_ERROR;
            }

            node_ptr n = cluster.find(address)->second;

            cluster.erase(address);
            auto cfg_entry = make_entry(serialize_configuration(), true);

            if (log->append(cfg_entry, true) != RC_GOOD)
            {
                LOG_ERROR << "del_server: failed to append configuration entry to log";
                prepare(rep, "ERROR " + leader_);

                return RC_ERROR;
            }

            if (address == self_id)
            {
                if (leadership_transfer(latest_server(), rep) == RC_GOOD)
                {
                    make_config(address, n, rep, server_removal);

                    return RC_GOOD;
                }
                else
                {
                    LOG_ERROR << "del_server: failed to transfer leadership of " << leader_;
                    prepare(rep, "ERROR " + leader_);

                    return RC_ERROR;
                }
            }
            else
            {
                make_config(address, n, rep, server_removal);
                append_entries();

                return RC_GOOD;
            }
        }

        void handle_transfer_leader_to(node* n)
        {
            if (n->match_index == log->last_entry_index())
                timeout_now(n);
        }

        void timeout_now(node* n)
        {
            auto task = std::make_shared<vote_task>();
            fill(task, n, ++timeout_now_id, election_rpc_timeout_, &raft_impl::on_timeout_now);

            invoke(n->request_vote_service, &rpc_service::timeout_now, task);
        }

        void on_timeout_now(vote_task_ptr task)
        {
            auto& ctl = task->controller;

            if (ctl.Failed())
            {
                LOG_DEBUG << "timeout_now " << task->id << " to server " << task->n->id << " failed: " << ctl.ErrorText();

                if (task->n->transfer_leader_to)
                {
                    LOG_INFO << "retry timeout_now to " << task->n->id;
                    timeout_now(task->n);
                }
            }
        }

        rc_errno leadership_transfer(std::string address, http_rep& rep)
        {
            make_id(address);

            if (role_ != LEADER)
            {
                LOG_INFO << "leadership_transfer: current role is not leader, ignoring";
                prepare(rep, "NOT_LEADER " + leader_);

                return RC_ERROR;
            }
            else if (address.empty())
            {
                LOG_INFO << "leadership_transfer: couldn't transfer leadership to server whose address is empty";
                prepare(rep, "EMPTY_SERVER " + leader_);

                return RC_ERROR;
            }
            else if (server_transfer_leader_to != nullptr)
            {
                LOG_INFO << "leadership_transfer: a leadership transfer is going on, ignoring";
                prepare(rep, "INPROGRESS " + leader_);

                return RC_ERROR;
            }
            else if (address == self_id)
            {
                LOG_INFO << "leadership_transfer: already leader, ignoring";
                prepare(rep, "IN_CFG " + leader_);

                return RC_ERROR;
            }
            else if (!cluster.contains(address))
            {
                LOG_INFO << "leadership_transfer: target server " << address << " isn't in the configuration, ignoring";
                prepare(rep, "NOT_IN_CFG " + leader_);

                return RC_ERROR;
            }

            auto n = cluster.find(address)->second;

            n->transfer_leader_to = true;
            server_transfer_leader_to = n.get();

            rep.ready = false;
            server_transfer_reply_to = &rep;

            LOG_INFO << "prepare to transfer leadership to " << n->id;

            if (n->match_index == log->last_entry_index())
                timeout_now(n.get());
            else
            {
                append_entries(n.get());

                leadership_transfer_timer.expires_from_now(boost::posix_time::milliseconds(min_election_timeout_));

                leadership_transfer_timer.async_wait([this, r = n.get()](const error_code_t& ec)
                {
                    on_leadership_transfer_timeout(r, ec);
                });
            }

            return RC_GOOD;
        }

        void on_leadership_transfer_timeout(node* n, const error_code_t& ec)
        {
            if (!ec)
            {
                LOG_INFO << "failed to transfer leadership to " << n->id << ", timed out";
                n->transfer_leader_to = false;

                if (recfg.get() && recfg->type == server_removal)
                {
                    complete_reconfiguration("TIMEOUT");
                    LOG_INFO << "recfg.reset() on_leadership_transfer_timeout";
                }
                else if (server_transfer_reply_to && !server_transfer_reply_to->ready)
                {
                    auto& rep = *server_transfer_reply_to;
                    prepare(rep, "TIMEOUT" + leader_);

                    rep.do_write();
                }

                server_transfer_reply_to = nullptr;
                server_transfer_leader_to = nullptr;
            }
            else if (ec.value() != boost::system::errc::operation_canceled)
                LOG_ERROR << "on_leadership_transfer_timeout error: " << ec.message();
            else
                LOG_TRACE << "leadership transfer timer canceled";
        }

        void adjust_configuration(const std::string& config)
        {
            if (config.empty())
                return;

            LOG_INFO << "adjust_configuration: " << config;
            std::vector<std::string> addresses = split(config, ",");

            for (auto& address : addresses)
            {
                 if (!cluster.contains(address))
                 {
                     node_ptr n = make_node(address, default_rpc_port);

                     cluster.emplace(address, n);
                     LOG_INFO << "adjust_configuration: added server " << address << " to configuration";
                 }
            }

            for (auto it = cluster.begin(); it != cluster.end(); )
            {
                 auto address = it->first;

                 if (std::find(addresses.begin(), addresses.end(), address) == addresses.end())
                 {
                     cluster.erase(it++);
                     LOG_INFO << "adjust_configuration: removed server " << address << " from configuration";
                 }
                 else
                     ++it;
            }
        }

        void apply_log_committed()
        {
            while (!log_committed_queue.pop())
            {
                while (last_applied_ < commit_index_)
                {
                    log_entry* entry = (*log)[++last_applied_];

                    if (!entry->cfg)
                        lcb(entry);
                }
            }
        }

        void apply_data_transfer()
        {
            while (!data_transfer_queue.pop())
                dcb(relative(path_), false);
        }

        std::string find_request_params(const std::string& params, const std::string& key)
        {
            std::vector<std::string> pairs = split(params, "&");

            for (auto& p : pairs)
            {
                 auto v = split(p, "=");

                 if (v.size() == 2 && v[0] == key)
                     return v[1];
            }

            return {};
        }

        rc_errno confirm(const http_req& req, http_rep& rep, const std::string& name)
        {
            auto method = req.method();
            auto target = req.target();

            if (method != http::verb::get && method != http::verb::head)
            {
                LOG_DEBUG << name << ": request method is not GET: " << method;
                fail(req, rep, "Unknown HTTP method", http::status::bad_request);

                return RC_ERROR;
            }

            if (target.empty() || target[0] != '/' || target.find("..") != beast::string_view::npos)
            {
                LOG_DEBUG << name << ": illegal request target: " << method;
                fail(req, rep, "illegal request target", http::status::bad_request);

                return RC_ERROR;
            }

            return RC_GOOD;
        }

        void prepare(http_rep& rep, const std::string& body)
        {
            auto& res = rep.res;

            if (!body.empty())
            {
                res.body() = body;
                res.prepare_payload();
            }
        }

        void prepare(const http_req& req, http_rep& rep, const std::string& body)
        {
            prepare(rep, body);
            auto& res = rep.res;

            res.result(http::status::ok);
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);

            res.set(http::field::content_type, "application/text");
            res.keep_alive(req.keep_alive());
        }

        void handle_show_servers(const http_req& req, http_rep& rep)
        {
            if (confirm(req, rep, "handle_show_servers") != RC_GOOD)
                return;

            auto config = serialize_configuration();

            if (config.empty())
                rep.res.prepare_payload();
            else
                prepare(req, rep, config);
       }

        void handle_show_states(const http_req& req, http_rep& rep)
        {
            if (confirm(req, rep, "handle_show_states") != RC_GOOD)
                return;

            auto now = clock_t::now();
            uint64_t secs = std::chrono::duration_cast<std::chrono::seconds>(now - started_at).count();

            std::string body;
            auto v = split(self_id, ":");

            body.append("host: " + v[0] + "\n");
            body.append("port: " + v[1] + "\n");

            std::string d = std::to_string(secs / 86400) + "d ";
            std::string h = std::to_string((secs / 3600) % 24) + "h ";

            std::string m = std::to_string((secs / 60) % 60) + "m ";
            std::string s = std::to_string(secs % 60) + "s";

            body.append("uptime: " + d + h + m + s + "\n");
            body.append("role: " + role_string() + "\n");

            body.append("index: " + std::to_string(commit_index_) + "\n");
            body.append("term: " + std::to_string(curr_term()) + "\n");

            body.append("leader: " + leader_ + "\n");
            body.append("servers: " + serialize_configuration() + "\n");

            if (role_ == LEADER)
            {
                std::string parameter = "http://" + leader_host() + ":" + http_port_ + "/leadership_transfer?server=";

                for (auto& [_, n] : cluster)
                {
                     if (n->id != self_id)
                         body.append(parameter + std::string(n->id)).append("\n");
                }
            }

            body.append("index\tterm\tdata\n");

            for (auto it = log->begin(); it != log->end(); ++it)
            {
                 body.append(std::to_string(it->index) + "\t");
                 body.append(std::to_string(it->term) + "\t");

                 body.append(it->str());

                 if (auto p = it; ++p != log->end())
                     body.append("\n");
            }

            prepare(req, rep, body);
        }

        void handle_request(const http_req& req, http_rep& rep, const std::string& op, const std::string& expr)
        {
            std::string name = op == "add" || op == "del" ? "handle_" + op + "_server" : op;

            if (confirm(req, rep, name) != RC_GOOD)
                return;

            std::string body;

            if (role_ != LEADER)
            {
                LOG_DEBUG << name << ": current role is not leader";
                body = "NOT_LEADER " + leader_;
            }
            else
            {
                auto url = req.target();
                size_t question_mark_pos = url.find_last_of('?');

                if (question_mark_pos == std::string::npos)
                {
                    LOG_DEBUG << name << ": parameter \'" << expr << "\' is empty or not found";
                    body = str_toupper("empty_" + expr + leader_);
                }
                else
                {
                    std::string params = url.substr(question_mark_pos + 1);
                    std::string target = find_request_params(params, expr);

                    if (target.empty())
                    {
                        LOG_DEBUG << name << ": parameter \'" << expr << "\' is empty or not found";
                        body = str_toupper("empty_" + expr + leader_);
                    }
                    else
                    {
                        rc_errno rc = RC_GOOD;

                        if (op == "add")
                            rc = add_server(target, rep);
                        else if (op == "del")
                            rc = del_server(target, rep);
                        else if (op == "handle_leadership_transfer")
                            rc = leadership_transfer(target, rep);
                        else if (op == "handle_append")
                        {
                            body = "OK ";

                            replace_all(target, "%20", " ");
                            auto entry = make_entry(target);

                            if (log->append(entry, true) != RC_GOOD)
                                body = "ERROR ";
                            else
                                LOG_INFO << op << ": log entry [" << target << "] appended";

                            body += leader_host() + ":" + http_port_;
                        }

                        if (rc != RC_GOOD)
                            LOG_INFO << name << ": errno " << rc;
                    }
                }
            }

            prepare(req, rep, body);
        }

        void handle_add_server(const http_req& req, http_rep& rep)
        {
            handle_request(req, rep, "add", "server");
        }

        void handle_del_server(const http_req& req, http_rep& rep)
        {
            handle_request(req, rep, "del", "server");
        }

        void handle_leadership_transfer(const http_req& req, http_rep& rep)
        {
            handle_request(req, rep, "handle_leadership_transfer", "server");
        }

        void handle_append(const http_req& req, http_rep& rep)
        {
            handle_request(req, rep, "handle_append", "content");
        }

        void register_http_service()
        {
            http_server->register_handler("/", [this](const http_req& req, http_rep& rep)
            {
                handle_show_states(req, rep);
            });

            http_server->register_handler("/show_states", [this](const http_req& req, http_rep& rep)
            {
                handle_show_states(req, rep);
            });

            http_server->register_handler("/show_servers", [this](const http_req& req, http_rep& rep)
            {
                handle_show_servers(req, rep);
            });

            http_server->register_handler("/append", [this](const http_req& req, http_rep& rep)
            {
                handle_append(req, rep);
            });

            http_server->register_handler("/add_server", [this](const http_req& req, http_rep& rep)
            {
                handle_add_server(req, rep);
            });

            http_server->register_handler("/del_server", [this](const http_req& req, http_rep& rep)
            {
                handle_del_server(req, rep);
            });

            http_server->register_handler("/leadership_transfer", [this](const http_req& req, http_rep& rep)
            {
                handle_leadership_transfer(req, rep);
            });
        }

        config_parser parser;

        log_committed_callback lcb;
        data_transfer_callback dcb;

        net::io_context ioc;
        work_ptr work_;

        net::deadline_timer election_timer;
        net::deadline_timer heartbeat_timer;

        net::deadline_timer leadership_transfer_timer;

        std::random_device device;
        std::mt19937 generator;

        rpc_channel_ptr request_vote_channel;
        rpc_channel_ptr data_transfer_channel;

        rpc_channel_ptr append_entries_channel;

        fs::path path_;
        fs::path parent_;

        std::string last;
        iterator_t range;

        iterator_t begin;
        iterator_t end;

        std::ifstream fin;
        std::array<char, buff_size> buff;

        mapped_file_ptr vote_data;
        entry_logger_ptr log;

        std::string interface_address;

        uint32_t min_election_timeout_;
        uint32_t max_election_timeout_;

        uniform_int_dist_ptr dist;

        uint32_t server_catch_up_rounds_;
        uint32_t election_rpc_timeout_;

        uint32_t heartbeat_rate_;
        uint32_t heartbeat_rpc_timeout_;

        uint32_t transfer_rpc_timeout_;
        uint32_t snapshot_rate_;

        bool log_compaction_enabled_;

        std::string rpc_host_;
        std::string rpc_port_;

        std::string self_id;

        rpc_server_ptr rpc_server;
        rpc_service_ptr rpc_service_;

        Closure* done_;

        std::string http_host_;
        std::string http_port_;

        http_server_ptr http_server;

        bool inited_ = false;
        bool started_ = false;

        std::mutex mutex_;

        std::string leader_;
        role_type role_ = FOLLOWER;

        node* server_transfer_leader_to = nullptr;
        http_rep* server_transfer_reply_to = nullptr;

        std::atomic<uint64_t> commit_index_ {0};
        std::atomic<uint64_t> last_applied_ {0};

        uint64_t votes;

        uint64_t pre_votes_granted;
        uint64_t pre_votes_refused;

        std::thread log_committed_thread;
        std::thread data_transfer_thread;

        concurrent_queue<bool> log_committed_queue;
        concurrent_queue<bool> data_transfer_queue;

        cluster_t cluster;
        reconfiguration_ptr recfg;

        uint64_t pre_vote_id = 0;
        uint64_t timeout_now_id = 0;

        uint64_t request_vote_id = 0;
        uint64_t append_entries_id = 0;

        uint64_t data_transfer_id = 0;
        uint64_t done_transfer_id = 0;

        time_point_t started_at;
        time_point_t last_heartbeat;
    };
}

#endif
