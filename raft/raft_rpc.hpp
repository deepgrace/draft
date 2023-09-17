//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef RAFT_RPC_HPP
#define RAFT_RPC_HPP

#include <fstream>
#include <filesystem>

namespace draft
{
    namespace fs = std::filesystem;
    namespace gp = google::protobuf;

    using Closure = gp::Closure;
    using RpcController = gp::RpcController;

    class rpc_service_impl : public rpc_service
    {
    public:
        using raft = raft_impl<rpc_service_impl>;

        rpc_service_impl(raft* r) : raft_(r)
        {
        }

        template <typename T, typename U>
        void set_result(const T* req, U* res, Closure* done, bool result)
        {
            auto& log = raft_->log;

            res->set_term(raft_->curr_term());
            res->set_success(result);

            if constexpr(requires { std::declval<U>().set_match_index(0); })
                res->set_match_index(log->last_entry_index());

            res->set_id(req->id());
            done->Run();
        }

        void timeout_now(RpcController* ctl, const vote_req* req, vote_res* res, Closure* done)
        {
            LOG_INFO << "leader " << req->candidate_id() << " tells to start a new election, about to do so";

            raft_->remove_election_timer();
            raft_->pre_vote(true);

            res->set_term(raft_->curr_term());
            res->set_vote_granted(false);

            done->Run();
        }

        void pre_vote(RpcController* ctl, const vote_req* req, vote_res* res, Closure* done)
        {
            auto now = clock_t::now();
            uint64_t msecs = std::chrono::duration_cast<std::chrono::milliseconds>(now - raft_->last_heartbeat).count();

            auto& log = raft_->log;

            if (log->last_entry_term() > req->last_log_term() || log->last_entry_index() > req->last_log_index())
            {
                res->set_term(raft_->curr_term());
                res->set_vote_granted(false);
            }
            else if (msecs < raft_->min_election_timeout_ && !req->early())
            {
                res->set_term(raft_->curr_term());
                res->set_vote_granted(false);
            }
            else
            {
                res->set_term(req->term());
                res->set_vote_granted(true);
            }

            done->Run();
        }

        void request_vote(RpcController* ctl, const vote_req* req, vote_res* res, Closure* done)
        {
            auto& peer = req->candidate_id();

            if (req->term() > raft_->curr_term())
            {
                LOG_INFO << "request_vote: a higher term " << req->term() << " from " << peer << " exists, stepping down";
                raft_->voted_for()[0] = 0;

                LOG_INFO << "calling step_down from rpc_service_impl::request_vote 1";
                raft_->step_down(req->term(), "");
            }

            auto& log = raft_->log;

            if (req->term() < raft_->curr_term())
            {
                LOG_INFO << "request_vote: request's term is smaller than current term, rejecting";

                res->set_term(raft_->curr_term());
                res->set_vote_granted(false);
            }
            else if (raft_->voted_for()[0] && peer != std::string(raft_->voted_for()))
            {
                LOG_INFO << "request_vote: already voted for " << raft_->voted_for() << ", rejecting request from " << peer;

                res->set_term(raft_->curr_term());
                res->set_vote_granted(false);
            }
            else if (log->last_entry_term() > req->last_log_term() || log->last_entry_index() > req->last_log_index())
            {
                LOG_INFO << "request_vote: requester " << peer << "'s log(" << req->last_log_index() << ","
                         << req->last_log_term() << ") is older than this server's log(" << log->last_entry_index()
                         << "," << log->last_entry_term() << "), rejecting";

                res->set_term(raft_->curr_term());
                res->set_vote_granted(false);
            }
            else
            {
                if (req->term() > raft_->curr_term())
                {
                    std::memcpy(raft_->voted_for(), peer.c_str(), raft_->voted_for_size());
                    LOG_INFO << "calling step_down from rpc_service_impl::request_vote 2";

                    raft_->step_down(req->term(), "");
                }
                else if (peer != std::string(raft_->voted_for()))
                {
                    raft_->curr_term() = req->term();
                    std::memcpy(raft_->voted_for(), peer.c_str(), raft_->voted_for_size());

                    raft_->sync_all();
                }

                LOG_INFO << "request_vote: granted vote for requester " << peer
                         << " log(" << req->last_log_index() << "," << req->last_log_term() << ")";

                res->set_term(req->term());
                res->set_vote_granted(true);
            }

            done->Run();
        }

        void append_entries(RpcController* ctl, const append_entries_req* req, append_entries_res* res, Closure* done)
        {
            rc_errno rc;
            bool need_adjust_cfg = false;

            if (req->term() < raft_->curr_term())
            {
                LOG_INFO << "append_entries: rpc from older leader " << req->leader_id() << " with term " << req->term() << ", rejecting";

                return set_result(req, res, done, false);
            }
            else if (req->term() > raft_->curr_term())
            {
                LOG_INFO << "append_entries: a new term " << req->term() << " from " << req->leader_id() << " begins, resetting election timer";
                LOG_INFO << "calling step_down from rpc_service_impl::append_entries";

                raft_->voted_for()[0] = 0;
                raft_->step_down(req->term(), req->leader_id());
            }
            else
            {
                raft_->reset_election_timer();
                raft_->role_ = FOLLOWER;
            }

            auto& log = raft_->log;

            if (!log->has_entry(req->prev_log_index(), req->prev_log_term()))
            {
                LOG_INFO << "append_entries: rpc from " << req->leader_id()
                         << " don't have such log entry as index:" << req->prev_log_index() << ", term: " << req->prev_log_term();

                return set_result(req, res, done, false);
            }
            else
            {
                if (req->entries_size())
                {
                    int i = 0;
                    LOG_INFO << "append_entries: about to append log entries: " << req->ShortDebugString();

                    for (; i < req->entries_size(); ++i)
                    {
                         const LogEntry& e = req->entries(i);
                         need_adjust_cfg = need_adjust_cfg || e.config();

                         if (log->entry_conflicted(e.index(), e.term()))
                         {
                             uint64_t old_cfg_entry_index = log->cfg_entry_index();

                             LOG_INFO << "append_entries: entry " << e.ShortDebugString()
                                      << " conflicted with existed one " << to_string((*log)[e.index()]) << ", chopping off";

                             if ((rc = log->chop(e.index(), true)) != RC_GOOD)
                             {
                                 LOG_ERROR << "append_entries: failed to chop entries starting at index " << e.index();
                                 ctl->SetFailed("server internal error");

                                 return set_result(req, res, done, false);
                             }

                             if (old_cfg_entry_index != log->cfg_entry_index())
                                 need_adjust_cfg = true;
                         }
                         else if (log->has_entry(e.index(), e.term()))
                         {
                             LOG_INFO << "append_entries: entry " << e.ShortDebugString() << " already existed in the log, skipping";
                             continue;
                         }

                         break;
                    }

                    std::string entries_content;
                    std::vector<log_entry_ptr> v;

                    for (; i < req->entries_size(); ++i)
                    {
                         const LogEntry& e = req->entries(i);

                         need_adjust_cfg = need_adjust_cfg || e.config();
                         auto entry = raft_->make_entry(e.data(), e.index(), e.term(), e.config());

                         v.push_back(entry);
                         entries_content.append(e.ShortDebugString());

                         if (i != req->entries_size() - 1)
                             entries_content.append(",");
                    }

                    if (v.size() && (rc = log->append(v, true)) != RC_GOOD)
                    {
                        LOG_ERROR << "append_entries: failed to append entries to log";
                        ctl->SetFailed("server internal error");

                        return set_result(req, res, done, false);
                    }

                    LOG_INFO << "append_entries: entries (" << entries_content << ") actually appened";
                }

                if (req->leader_commit() > raft_->commit_index_)
                {
                    LOG_INFO << "append_entries: set commit index to min(leader_commit[" << req->leader_commit() 
                             << "], last_entry_index[" << log->last_entry_index() << "])";

                    raft_->commit_index_ = std::min(req->leader_commit(), log->last_entry_index());
                    raft_->log_committed_queue.push(false);
                }
            }

            raft_->leader_ = req->leader_id();
            raft_->last_heartbeat = clock_t::now();

            if (need_adjust_cfg)
            {
                LOG_INFO << "append_entries: configuration entry changed, adjusting";
                raft_->adjust_configuration(log->config());
            }

            set_result(req, res, done, true);
        }

        void data_transfer(RpcController* ctl, const data_req* req, data_res* res, Closure* done)
        {
            if (req->term() < raft_->curr_term())
            {
                LOG_INFO << "data_transfer: rpc from older leader " << req->leader_id() << " with term " << req->term() << ", rejecting...";

                return set_result(req, res, done, false);
            }
            else if (req->term() > raft_->curr_term())
            {
                LOG_INFO << "data_transfer: a new term " << req->term() << " from " << req->leader_id() << " begins, resetting election timer";
                LOG_INFO << "calling step_down from rpc_service_impl::data_transfer";

                raft_->voted_for()[0] = 0;
                raft_->step_down(req->term(), req->leader_id());
            }

            raft_->role_ = FOLLOWER;
            raft_->leader_ = req->leader_id();

            std::string name = req->name();
            size_t size = name.size();

            if (req->id() == 1)
            {
                LOG_INFO << "data_transfer: rpc from current leader " << req->leader_id() << ", remove election timer at first time";

                raft_->remove_election_timer();

                auto& log = raft_->log;
                uint64_t last = log->last_index() + 1;

                if (raft_->log_compaction_enabled())
                {
                    log->chop(last, true);
                    auto& cfg = req->last_config();

                    uint64_t term = req->last_term();
                    uint64_t index = req->last_index();

                    raft_->commit_index_ = index;
                    raft_->last_applied_ = index;

                    raft_->adjust_configuration(cfg);
                    log->copy(make_states(cfg), index, term);
                }

                if (size)
                {
                    auto p = name.find_first_of('/');

                    if (p == std::string::npos)
                        p = size - 1;

                    raft_->data_transfer_cb()(name.substr(0, p + 1), true);
                }
            }

            set_result(req, res, done, true);

            if (!size)
                return;

            bool set_perms = false;
            bool is_dir = name.back() == '/';

            if (is_dir)
                name.pop_back();

            if (name != last)
            {
                set_perms = true;
                last = name;

                LOG_INFO << "receiving " << name;
            }

            if (is_dir)
                fs::create_directories(name);
            else
            {
                if (set_perms)
                {
                    if (auto parent = fs::path(name).parent_path(); !parent.empty())
                        fs::create_directories(parent);

                    if (fout.is_open())
                        fout.close();

                    fout.open(name, std::ios_base::app | std::ios_base::binary);
                }

                auto& data = req->data();
                fout.write(data.c_str(), data.size());
            }

            if (set_perms)
                fs::permissions(name, fs::perms(req->perms()));
        }

        void done_transfer(RpcController* ctl, const done_req* req, data_res* res, Closure* done)
        {
            if (req->term() < raft_->curr_term())
            {
                LOG_INFO << "done_transfer: rpc from older leader " << req->leader_id() << " with term " << req->term() << ", rejecting";

                return set_result(req, res, done, false);
            }
            else if (req->term() > raft_->curr_term())
            {
                LOG_INFO << "done_transfer: a new term " << req->term() << " from " << req->leader_id() << " begins, resetting election timer";
                LOG_INFO << "calling step_down from rpc_service_impl::done_transfer";

                raft_->voted_for()[0] = 0;
                raft_->step_down(req->term(), req->leader_id());
            }

            raft_->role_ = FOLLOWER;
            raft_->leader_ = req->leader_id();

            LOG_INFO << "receiving done";
            set_result(req, res, done, true);

            std::string name = req->name();
            size_t size = name.size();

            if (size)
            {
                auto p = name.find_first_of('/');

                if (p == std::string::npos)
                    p = size;

                raft_->set_path(name.substr(0, p));
                LOG_INFO << "transferred path is " << raft_->path();

                if (fs::exists(raft_->path()))
                    raft_->data_transfer_queue.push(false);
            }

            if (fout.is_open())
                fout.close();

            raft_->last_heartbeat = clock_t::now();
        }

        void done()
        {
        }

        ~rpc_service_impl()
        {
        }

    private:
        raft* raft_;

        std::string last;
        std::ofstream fout;
    };
}

#endif
