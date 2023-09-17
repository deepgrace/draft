//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef ENTRY_ITERATOR_HPP
#define ENTRY_ITERATOR_HPP

#include <string>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace draft
{
    inline bool is_ipv4(const std::string& str)
    {
        auto buff = std::make_unique<char[]>(str.size() + 1);
        std::strcpy(buff.get(), str.c_str());

        char* curr;
        char* next = strtok_r(buff.get(), ".", &curr);

        int count = 0;

        while (next)
        {
            ++count;

            try
            {
                if (auto field = std::stol(next); field > 255 || field < 0)
                    return false;
            }
            catch (...)
            {
                return false;
            }

            next = strtok_r(NULL, ".", &curr);
        }

        return count == 4;
    }

    inline std::string get_ipv4(const std::string& ifn)
    {
        char buff[30];
        struct ifreq ifr;

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        std::strcpy(ifr.ifr_name, ifn.c_str());

        if (ioctl(fd, SIOCGIFADDR, &ifr) < 0)
        {
            LOG_ERROR << "faield in ioctl " << std::strerror(errno);
            close(fd);

            return std::string();
        }

        if (inet_ntop(AF_INET, &(((sockaddr_in*)&ifr.ifr_addr)->sin_addr), buff, sizeof(buff)) == NULL)
        {
            LOG_ERROR << "failed in inet_ntop " << std::strerror(errno);
            close(fd);

            return std::string();
        }

        return std::string(buff);
    }

    template <typename T, typename U>
    struct entry_iterator
    {
        entry_iterator() : t(nullptr), u(U())
        {
        }

        entry_iterator(T* t) : t(t), u(t->size)
        {
        }

        entry_iterator(T* t, U u) : t(t), u(u)
        {
        }

        bool operator!=(const entry_iterator& rhs)
        {
             return u != rhs.u;
        }

        bool operator==(const entry_iterator& rhs)
        {
             return !(*this != rhs);
        }

        entry_iterator& operator++()
        {
            t = static_cast<T*>((void*)((char*)t + u));
            u = t->size;

            return *this;
        }

        entry_iterator& operator=(const entry_iterator& rhs)
        {
            t = rhs.t;
            u = rhs.u;

            return *this;
        }

        decltype(auto) operator->()
        {
            return t;
        }

        decltype(auto) operator*()
        {
            return *operator->();
        }   

        T* t;
        U  u;
    };
}

#endif
