//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef STORAGE_SERVER_HPP
#define STORAGE_SERVER_HPP

#include <map>
#include <set>
#include <memory>
#include <sstream>
#include <sys/wait.h>
#include <draft.hpp>
#include <command_parser.hpp>

namespace net = boost::asio;
namespace fs = std::filesystem;

using tcp = net::ip::tcp;

using socket_t = tcp::socket;
using error_code_t = boost::system::error_code; 

namespace kv
{
    template <typename T>
    class session : public std::enable_shared_from_this<session<T>>
    {
    public:
        session(const session&) = delete;
        session& operator=(const session&) = delete;

        using buffer_t = std::array<char, 8192>;
        using command_handler = std::function<void(std::shared_ptr<session<T>>, const command&)>;

        explicit session(T& t, socket_t socket, command_handler handler) : t(t), socket(std::move(socket)), handler(handler)
        {
        }

        constexpr decltype(auto) shared_this()
        {
            return this->shared_from_this();
        }

        void start()
        {
            do_read();
        }

        void do_read()
        {
            socket.async_read_some(net::buffer(request),
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_read(ec, bytes_transferred);
            });
        }

        void on_read(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                begin = request.data();
                end = begin + bytes_transferred;

                parse_command();
            }
            else
                t.remove(shared_this());
        }

        void do_write(const std::string& status, const std::string& data)
        {
            reply = status + " " + data + "\r\n";

            net::async_write(socket, net::buffer(reply.data(), reply.size()),
            [self = shared_this()](error_code_t ec, std::size_t bytes_transferred)
            {
                self->on_write(ec, bytes_transferred);
            });
        }

        void on_write(error_code_t ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                cmd_.reset();
                parser.reset();

                do_read();
            }
            else
                t.remove(shared_this());
        }

        void parse_command()
        {
            result_type result;
            std::tie(result, begin) = parser.parse(cmd_, begin, end);

            if (result == good)
                handler(shared_this(), cmd_);
            else if (result == bad)
                do_write("INVALID_COMMAND", "");
            else if (result == need_more || begin == end)
                do_read();
        }

        command& cmd()
        {
            return cmd_;
        }

    private:
        T& t;
        socket_t socket;

        char* begin;
        char* end;

        command cmd_;

        buffer_t request;
        std::string reply;

        command_parser parser;
        command_handler handler;
    };

    class storage_server
    {
    public:
        storage_server(const storage_server&) = delete;
        storage_server& operator=(const storage_server&) = delete;

        using session_t = session<storage_server>;
        using session_ptr  = std::shared_ptr<session_t>;

        using commands_t = std::unordered_map<uint64_t, session_ptr>;
        using database_t = std::unordered_map<std::string, std::string>;

        explicit storage_server(const std::string& host, const std::string& port, const std::string& conf) :
        acceptor(ioc, tcp::endpoint(net::ip::make_address(host), std::stoi(port))), raft_(conf,
        [this](const draft::log_entry* entry){ on_log_entry_committed(entry); },
        [this](const std::string& path, bool pre_transfer){ on_data_transfer(path, pre_transfer); })
        {
        }

        int init()
        {
            if (raft_.init() != RC_GOOD)
                return -1;

            set_path(raft_.path());
            set_snapshot_rate(raft_.snapshot_rate());

            if (load_database() != RC_GOOD)
                return -1;

            if (update_snapshot_size() != RC_GOOD)
                return -1;

            return 0;
        }

        int load_database()
        {
            if (!fs::exists(path_) || !fs::file_size(path_))
                return RC_GOOD;

            draft::config_parser parser(path_);

            if (parser.parse() != RC_GOOD)
                return  -1;

            LOG_INFO << "loading database...";
            std::lock_guard<std::mutex> g(mute);

            for (auto& [key, value] : parser.config())
                 db[key] = value;

            for (auto& [key, value] : db)
                 LOG_INFO << key << "\t" << value;

            return RC_GOOD;
        }

        int drop_database()
        {
            LOG_INFO << "dropping database...";
            std::lock_guard<std::mutex> g(mute);

            db.clear();
            fs::remove_all(path_);

            return RC_GOOD;
        }

        void start()
        {
            snapshot_stopped = false;

            raft_thread_ = std::thread([this]{ raft_thread(); });
            snapshot_thread_ = std::thread([this]{ snapshot_thread(); });

            do_accept();

            try
            {
                ioc.run();
            }
            catch (std::exception& e)
            {
                LOG_ERROR << e.what();
            }
        }

        void do_accept()
        {
            acceptor.async_accept(
            [this](error_code_t ec, socket_t socket)
            {
                on_accept(ec, std::move(socket));
            });
        }

        void on_accept(error_code_t ec, socket_t socket)
        {
            if (!ec)
            {
                auto f = [this](session_ptr s, const command& cmd){ handle_command(s, cmd); };
                auto s = std::make_shared<session_t>(*this, std::move(socket), f);

                sessions.insert(s);
                s->start();
            }

            do_accept();
        }

        void handle_command(session_ptr s, const command & cmd)
        {
            if (cmd.type == kv::get)
            {
                auto it = db.find(cmd.key);
                s->do_write("OK", it != db.end() ? it->second : "null");
            }
            else if (cmd.type == kv::set || cmd.type == kv::del)
            {
                if (raft_.role() != draft::LEADER)
                    s->do_write("NOT_LEADER", raft_.leader());
                else
                {
                    std::string log = cmd.type == kv::set ? "set " : "del ";
                    log.append(cmd.key);

                    if (cmd.type == kv::set)
                        log.append(" " + cmd.value);

                    uint64_t index = raft_.append(log);

                    if (index == 0)
                        s->do_write("INTERNAL_ERROR", "");
                    else
                        commands.insert(std::make_pair(index, s));
                }
            }
        }

        void on_log_entry_committed(const draft::log_entry* entry)
        {
            auto index = entry->index;
            auto it = commands.find(index);

            if (it == commands.end())
                apply(entry);
            else
            {
                insert(it);
                commands.erase(it);
            }

            if (raft_.log_compaction_enabled() && index == raft_.commit_index())
            {
                auto b = raft_.drop(index, true) == RC_GOOD;
                LOG_INFO << "applying log compaction at index " << index << " " << (b ? "succeed" : "failed");
            }
        }

        void on_data_transfer(const std::string& path, bool pre_transfer)
        {
            std::string type;
            std::string file = path;

            std::string banner = (pre_transfer ? "preparing: drop" : "restoring: load");

            if (pre_transfer)
            {
                if (file.back() == '/')
                {
                    file.pop_back();

                    type = "dir";
                    set_path(file);
                }
                else
                {
                    type = "file";
                    set_path(file);

                    LOG_INFO << "on_data_transfer " << banner << " database from " << type << " " << file;
                    drop_database();
                }
            }
            else
            {
                set_path(file);

                if (fs::is_directory(file))
                    type = "dir";
                else if (fs::is_regular_file(file))
                {
                    type = "file";

                    LOG_INFO << "on_data_transfer " << banner << " database from " << type << " " << file;
                    load_database();
                }
            }
        }

        void apply(const draft::log_entry* entry)
        {
            LOG_INFO << "session not found at index " << entry->index;

            std::string key;
            std::string value;

            std::string type;
            std::istringstream input(entry->str());

            std::ostringstream os;
            os.imbue(std::locale::classic());

            input >> type;

            if (type == "set")
            {
                input >> key;
                input.get();

                std::getline(input, value);
                os << "set [" << key << "] [" << value << "]' to";

                std::lock_guard<std::mutex> g(mute);
                db[key] = value;
            }
            else if (type == "del")
            {
                input >> key;
                os << "'del [" << key << "]' on";

                std::lock_guard<std::mutex> g(mute);
                db.erase(key);
            }

            LOG_INFO << "restoring: applying " << os.str() << " the database";
        }

        void insert(auto it)
        {
            auto& s = it->second;
            auto& cmd = s->cmd();

            std::ostringstream os;
            os.imbue(std::locale::classic());

            if (cmd.type != kv::get && cmd.type != kv::unknown)
            {
                std::lock_guard<std::mutex> g(mute);
                std::string old;

                if (auto it = db.find(cmd.key); it != db.end())
                    old = std::move(it->second);

                if (cmd.type == kv::set)
                {
                    db[cmd.key] = cmd.value;
                    os << "set [" << cmd.key << "] [" << cmd.value << "]' to";
                }
                else
                {
                    db.erase(cmd.key);
                    os << "'del [" << cmd.key << "]' on";
                }

                s->do_write("OK", (cmd.type == kv::set ? old : ""));
                LOG_INFO << "on log entry committed: applying " << os.str() << " the database";
            }
        }

        void set_path(const std::string& path)
        {
            path_ = path;
        }

        void set_snapshot_rate(uint32_t n)
        {
            snapshot_rate_ = n;
        }

        void remove(session_ptr s)
        {
            sessions.erase(s);
        }

        void stop()
        {
            snapshot_stopped = true;

            ioc.stop();
            raft_.stop();

            raft_thread_.join();
            snapshot_thread_.join();
        }

        void raft_thread()
        {
            raft_.start();
        }

        void snapshot_thread()
        {
            while (snapshot_stopped == false)
            {
                int secs = raft_.log_compaction_enabled() ? snapshot_rate_ : 1;

                std::this_thread::sleep_for(std::chrono::seconds(secs));
                LOG_DEBUG << "prev_snapshot_size: " << prev_snapshot_size << ", raft log size : " << raft_.log_size();

                if (!raft_.log_compaction_enabled())
                {
                    while (prev_snapshot_size * 4 >= raft_.log_size())
                           std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                int pid;

                {
                    std::lock_guard<std::mutex> g(mute);

                    switch (pid = fork())
                    {
                        case -1:
                            LOG_FATAL << "failed to fork snapshotting process " << std::strerror(errno);
                            exit(-1);
                        break;

                        case 0:
                            take_snapshot();

                            close(STDIN_FILENO);
                            close(STDOUT_FILENO);
                            close(STDERR_FILENO);

                            execlp("echo", "echo", NULL);
                            LOG_FATAL << "snapshotting-process: failed to execl(\"echo\", \"echo\", NULL);" << std::strerror(errno);

                            exit(-1);
                        break;

                        default:
                        break;
                    }
                }

                if (pid > 0)
                {
                    int stat;

                    if (wait(&stat) == -1)
                    {
                        LOG_FATAL << "failed to wait for snapshotting process [" << pid << "] to terminate, " << std::strerror(errno);
                        exit(-1);
                    }

                    update_snapshot_size();
                }
            }
        }

        void take_snapshot()
        {
            FILE* f = fopen(path_.c_str(), "w+");

            if (f == nullptr)
            {
                LOG_INFO << "snapshotting-process: failed to open snapshot file " << path_;
                exit(-1);
            }

            LOG_DEBUG << "snapshotting-process: taking a snapshot of current system";

            for (auto& [key, val] : db)
                 fprintf(f, "%s = %s\n", key.c_str(), val.c_str());

            fclose(f);
        }

        int update_snapshot_size()
        {
            struct stat st;
            snapshot_fd = open(path_.c_str(), O_RDWR | O_CREAT, 0644);

            if (snapshot_fd < 0)
            {
                LOG_ERROR << "failed to open snapshot file " << path_ << " " << std::strerror(errno);

                return RC_ERROR;
            }

            if (fstat(snapshot_fd, &st) == -1)
            {
                LOG_ERROR << "failed to fstat() on file " << path_ << " " << std::strerror(errno);

                return RC_ERROR;
            }

            LOG_DEBUG << "updated prev_snapshot_size from " << prev_snapshot_size << " to " << st.st_size;
            prev_snapshot_size = st.st_size;

            close(snapshot_fd);

            return RC_GOOD;
        }

        ~storage_server()
        {
            close(snapshot_fd);
        }

    private:
        net::io_context ioc;
        tcp::acceptor acceptor;

        draft::raft raft_;
        uint32_t snapshot_rate_;

        std::string path_;
        std::set<session_ptr> sessions;

        std::thread raft_thread_;
        std::thread snapshot_thread_;

        database_t db;
        int snapshot_fd = -1;

        bool snapshot_stopped = false;
        uint64_t prev_snapshot_size = 0;

        std::mutex mute;
        commands_t commands;
    };
}

#endif
