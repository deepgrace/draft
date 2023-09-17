//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef ENTRY_LOGGER_HPP
#define ENTRY_LOGGER_HPP

#include <memory>
#include <vector>
#include <utils.hpp>
#include <mapped_file.hpp>
#include <entry_iterator.hpp>

namespace draft
{
    constexpr int log_entry_alignment = 8;
    constexpr uint64_t log_end_marker = 0;

    struct log_entry
    {
        std::string str()
        {
            int offset = cfg * sizeof(uint64_t);

            return std::string(data + offset, len - offset);
        }

        std::string str() const
        {
            return const_cast<log_entry*>(this)->str();
        }

        uint64_t size;
        uint64_t index;

        uint64_t term;
        bool cfg;

        uint64_t len;
        char data[];
    };

    struct states
    {
        uint64_t size;
        uint64_t last_index;

        uint64_t last_term;
        uint64_t last_len;

        char last_config[];
    };

    struct log_entry_info
    {
        uint64_t index;
        uint64_t amount;
    };

    constexpr size_t log_entry_size = sizeof(log_entry);

    constexpr size_t log_end_marker_size = sizeof(log_end_marker);
    constexpr log_entry log_entry_sentry {log_entry_size, 0, 0, 0};

    constexpr size_t default_stat_size = sizeof(states);
    constexpr size_t default_ldbs_size = log_entry_size + log_end_marker_size;

    using states_ptr = std::shared_ptr<states>;
    using log_entry_ptr = std::shared_ptr<log_entry>;

    template <typename T>
    std::string to_string(T t)
    {
        std::string str;

        str.append("[size: ").append(std::to_string(t->size)).append(", ");
        str.append("index: ").append(std::to_string(t->index)).append(", ");

        str.append("term: ").append(std::to_string(t->term)).append(", ");
        str.append("cfg: ").append(std::to_string(t->cfg)).append(", ");

        str.append("data: '").append(t->str()).append("']");

        return str;
    }

    inline log_entry_ptr make_log_entry(uint64_t len, bool cfg = false)
    {
        size_t offset = cfg * sizeof(uint64_t);

        uint64_t total_len = log_entry_size + offset + len;
        total_len = (total_len + log_entry_alignment - 1) & ~(log_entry_alignment - 1);

        auto deleter = [](log_entry* e){ delete [] static_cast<char*>(static_cast<void*>(e)); };
        log_entry_ptr e(static_cast<log_entry*>((void*)(new char[total_len])), deleter);

        std::memset(e.get(), 0, total_len);

        e->size = total_len;
        e->len = len + offset;

        return e;
    }

    inline states_ptr make_states(const std::string& data)
    {
        uint64_t last_len = data.size();

        if (!last_len)
            return nullptr;

        uint64_t total_len = sizeof(states) + last_len;
        total_len = (total_len + log_entry_alignment - 1) & ~(log_entry_alignment - 1);

        auto deleter = [](states* s){ delete [] static_cast<char*>(static_cast<void*>(s)); };
        states_ptr s(static_cast<states*>((void*)(new char[total_len])), deleter);

        std::memset(s.get(), 0, total_len);

        s->size = total_len;
        s->last_len = last_len;

        std::memcpy(s->last_config, data.c_str(), last_len);

        return s;
    }

    class entry_logger
    {
    public:
        using infos_t = std::vector<log_entry_info>;
        using iterator = entry_iterator<log_entry, uint64_t>;

        entry_logger(const entry_logger&) = delete;
        entry_logger& operator=(const entry_logger&) = delete;

        entry_logger(auto&& stat_file, auto&& ldbs_file, double factor = 1.25) :
        stat_map_(stat_file, 0, default_stat_size), ldbs_map_(ldbs_file, 0, default_ldbs_size), factor(factor)
        {
            if (factor < 1.0)
                factor = 1.25;
        }

        rc_errno init()
        {
            rc_errno rc = ldbs_map_.map();

            if (rc == RC_MMAP_NEW_FILE)
            {
                LOG_INFO << "first boot, fill in sentry log entry";

                log_entry* e = ldbs_map();
                log_entry sentry = log_entry_sentry;

                std::memcpy(e, &sentry, sentry.size);
                ldbs_map_.sync_all();
            }
            else if (rc != RC_GOOD)
                return rc;

            ldbs_file = ldbs_map_.filename();
            ldbs_map_.advise(MADV_SEQUENTIAL);

            for (auto it = begin(); it != end(); ++it)
            {
                 if (it->cfg)
                     cfg_entry_index_ = it->index;

                 infos.emplace_back(it->index, payload_size_);
                 payload_size_ += it->size;
            }

            ldbs_size = ldbs_map_.size();
            ldbs_map_.advise(MADV_NORMAL);

            rc_errno rc2 = stat_map_.map();

            if (rc2 == RC_MMAP_NEW_FILE)
            {
                LOG_INFO << "first boot, fill in last config states";

                states t{sizeof(states), 0, 0, 0};
                states* s = stat_map();

                std::memcpy(s, &t, t.size);
                stat_map_.sync_all();
            }
            else if (rc2 != RC_GOOD)
                return rc2;

            stat_file = stat_map_.filename();
            stat_size = stat_map_.size();

            LOG_INFO << "raft log size: " << ldbs_size;
            LOG_INFO << "init last_index: " << last_index();

            return RC_GOOD;
        }

        std::string to_string()
        {
            std::string s;
            ldbs_map_.advise(MADV_SEQUENTIAL);

            for (auto it = begin(), next = it; it != end(); ++it)
            {
                 ++next;
                 s.append(it->str());

                 if (it != begin() && next != end())
                     s.append(",");
            }

            ldbs_map_.advise(MADV_NORMAL);

            return s;
        }

        rc_errno append(log_entry_ptr entry, bool sync = false)
        {
            return append(std::vector<log_entry_ptr>{entry}, sync);
        }

        rc_errno append(const std::vector<log_entry_ptr>& entries, bool sync = false)
        {
            uint64_t size_bytes = 0;

            for (auto& e : entries)
                 size_bytes += e->size;

            while (ldbs_size - payload_size_ - log_end_marker_size <= size_bytes)
            {
                if (auto rc = grow_ldbs(); rc != RC_GOOD)
                    return rc;
            }

            uint64_t pos = infos.back().amount;

            LOG_INFO << "appending entires total size: " << size_bytes
                     << ", available size: " << ldbs_size - payload_size_ - log_end_marker_size
                     << ", payload size: " << payload_size_
                     << ", entries size: " << pos;

            log_entry* next = nullptr;
            log_entry* tail = last_entry();

            for (auto& e : entries)
            {
                 if (e->cfg)
                     cfg_entry_index_ = e->index;

                 next = static_cast<log_entry*>((void*)((char*)tail + tail->size));
                 std::memcpy((char*)next, (char*)e.get(), e->size);

                 pos += tail->size;
                 infos.emplace_back(e->index, pos);

                 tail = next;
            }

            tail = last_entry();
            next = static_cast<log_entry*>((void*)((char*)tail + tail->size));

            next->size = log_end_marker;
            payload_size_ += size_bytes;

            if (sync)
                return ldbs_map_.sync_range((char*)next - size_bytes, size_bytes + log_end_marker_size);

            return RC_GOOD;
        }

        rc_errno chop(uint64_t index, bool sync = false)
        {
            if (!index || infos.empty() || index < infos.front().index || index > infos.back().index || (infos.size() > 1 && index < infos[1].index))
                return RC_OOR;

            while (index <= cfg_entry_index_)
            {
                uint64_t prev_cfg_index;
                auto cfg_entry = config_entry();

                std::memcpy(&prev_cfg_index, cfg_entry->data, sizeof(uint64_t));
                cfg_entry_index_ = ntoh64(prev_cfg_index);
            }

            log_entry_info& m = infos[index - last_index() - infos.front().index];
            log_entry* e = static_cast<log_entry*>((void*)((char*)ldbs_map_.addr() + m.amount));

            e->size = log_end_marker;

            payload_size_ = m.amount;
            infos.erase(infos.begin() + index - last_index(), infos.end());

            if (sync)
                return ldbs_map_.sync_range((char*)e, log_end_marker_size);

            return RC_GOOD;
        }

        rc_errno drop(uint64_t index, bool sync = false)
        {
            if (!index || infos.empty() || index < infos.front().index || index > infos.back().index || (infos.size() > 1 && index < infos[1].index))
                return RC_OOR;

            log_entry* cfg_entry = nullptr;
            uint64_t curr_cfg_index = cfg_entry_index_;

            while (curr_cfg_index && index < curr_cfg_index)
            {
                uint64_t prev_cfg_index = 0;
                cfg_entry = operator[](curr_cfg_index);

                std::memcpy(&prev_cfg_index, cfg_entry->data, sizeof(uint64_t));
                curr_cfg_index = ntoh64(prev_cfg_index);
            }

            if (index >= cfg_entry_index_)
                cfg_entry_index_ = 0;

            std::string cfg_data;

            if (curr_cfg_index && ((cfg_entry = operator[](curr_cfg_index)) != nullptr))
            {
                cfg_data = cfg_entry->str();
                LOG_INFO << "last config data up through Index " << index << ": " << cfg_data << std::endl;
            }

            uint64_t term = operator[](index)->term;

            if (index == infos.back().index)
            {
                if (auto rc = chop(last_index() + 1, sync); rc != RC_GOOD)
                    return rc;

                return copy(make_states(cfg_data), index, term);
            }

            uint64_t next = index + 1;
            uint64_t front = infos.front().index;

            uint64_t first = infos[1 - front].amount;
            uint64_t last = infos.back().amount;

            uint64_t begin = infos[next - last_index() - front].amount;
            uint64_t end = last + last_entry()->size;

            uint64_t bytes_moved = end - begin;
            uint64_t bytes_dropped = begin - first;

            payload_size_ -= bytes_dropped;

            log_entry* curr = operator[](next);
            log_entry* head = operator[](last_index() + 1);

            std::memmove(head, curr, bytes_moved);

            auto it = infos.begin() + 1;
            infos.erase(it, it + index - last_index());

            for (auto it = infos.begin() + 1; it != infos.end(); ++it)
                 it->amount -= bytes_dropped;

            log_entry* tail = static_cast<log_entry*>((void*)((char*)ldbs_map_.addr() + end - bytes_dropped));
            tail->size = log_end_marker;

            if (auto rc = copy(make_states(cfg_data), index, term); rc != RC_GOOD)
                return rc;

            if (sync)
                return ldbs_map_.sync_range((char*)head, bytes_moved + log_end_marker_size);

            LOG_INFO << "last config data is " << last_config();

            return RC_GOOD;
        }

        uint64_t payload()
        {
            return payload_size_;
        }

        uint64_t size()
        {
            return ldbs_size;
        }

        uint64_t entries()
        {
            return infos.size();
        }

        log_entry* operator[](uint64_t index)
        {
            if (infos.empty() || index < infos.front().index || index > infos.back().index || (index && infos.size() > 1 && index < infos[1].index))
                return nullptr;

            if (index == 0)
                return first_entry();

            uint64_t front = infos.front().index;
            log_entry_info& m = infos[index - last_index() - front];

            return static_cast<log_entry*>((void*)((char*)ldbs_map_.addr() + m.amount));
        }

        bool has_entry(uint64_t index, uint64_t term)
        {
            log_entry* entry = operator[](index);

            if (entry == nullptr)
            {
                if (last_index() != index || last_term() != term)
                    return false;
            }
            else if (entry->index != index || entry->term != term)
                return false;

            return true;
        }

        bool entry_conflicted(uint64_t index, uint64_t term)
        {
            log_entry* entry = operator[](index);

            if (entry == nullptr)
                return false;

            return entry->term != term;
        }

        std::string config()
        {
            log_entry* e = config_entry();

            return e == nullptr ? std::string() : e->str();
        }

        void copy(log_entry* entry, const std::string& data, bool cfg = false)
        {
            int offset = cfg * sizeof(uint64_t);

            if (cfg)
            {
                uint64_t prev_cfg_index = hton64(cfg_entry_index_);
                std::memcpy(entry->data, &prev_cfg_index, offset);
            }

            std::memcpy(entry->data + offset, data.c_str(), entry->len - offset);
        }

        log_entry* config_entry()
        {
            LOG_INFO << "config_entry: cfg_entry_index_ " << cfg_entry_index_;

            return operator[](cfg_entry_index_);
        }

        uint64_t cfg_entry_index()
        {
            return cfg_entry_index_;
        }

        log_entry* first_entry()
        {
            return static_cast<log_entry*>((void*)(((char *)ldbs_map_.addr()) + infos.front().amount));
        }

        uint64_t first_entry_index()
        {
            return first_entry()->index;
        }

        uint64_t first_entry_term()
        {
            return first_entry()->term;
        }

        log_entry* last_entry()
        {
            return static_cast<log_entry*>((void*)((char*)ldbs_map_.addr() + infos.back().amount));
        }

        uint64_t last_entry_index()
        {
            if (infos.size() == 1)
                return last_index();

            return last_entry()->index;
        }

        uint64_t last_entry_term()
        {
            if (infos.size() == 1)
                return last_term();

            return last_entry()->term;
        }

        uint64_t last_entry_size()
        {
            return last_entry()->size;
        }

        uint64_t last_entry_len()
        {
            return last_entry()->len;
        }

        inline states* stat_map()
        {
            return static_cast<states*>(stat_map_.addr());
        }

        inline log_entry* ldbs_map()
        {
            return static_cast<log_entry*>(ldbs_map_.addr());
        }

        uint64_t last_index()
        {
            return stat_map()->last_index;
        }

        uint64_t last_term()
        {
            return stat_map()->last_term;
        }

        std::string last_config()
        {
            states* s = stat_map();

            return std::string(s->last_config, s->last_len);
        }

        rc_errno copy(states_ptr p, uint64_t index, uint64_t term)
        {
            states* s = stat_map();

            if (p != nullptr)
            {
                while (stat_size - sizeof(states) < p->last_len)
                {
                    if (auto rc = grow_stat(); rc != RC_GOOD)
                        return rc;
                }

                p->last_index = index;
                p->last_term = term;

                s = stat_map();
                std::memcpy((char*)s, (char*)p.get(), p->size);
            }
            else
            {
                s->last_index = index;
                s->last_term = term;
            }

            return stat_map_.sync_range(s, s->size);
        }

        iterator begin()
        {
            return ldbs_map();
        }

        iterator end()
        {
            return { nullptr, log_end_marker };
        }

    private:
        rc_errno grow_ldbs()
        {
            auto rc = ldbs_map_.remap(ldbs_size * factor);

            if (rc != RC_GOOD)
                return rc;

            ldbs_size = ldbs_map_.size();

            return rc;
        }

        rc_errno grow_stat()
        {
            auto rc = stat_map_.remap(stat_size * factor);

            if (rc != RC_GOOD)
                return rc;

            stat_size = stat_map_.size();

            return rc;
        }

        uint64_t ldbs_size;
        uint64_t stat_size;

        std::string stat_file;
        std::string ldbs_file;

        mapped_file stat_map_;
        mapped_file ldbs_map_;

        double factor;
        infos_t infos;

        uint64_t payload_size_ = 0;
        uint64_t cfg_entry_index_ = 0;
    };
}

#endif
