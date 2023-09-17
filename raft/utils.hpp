//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef UTILS_HPP
#define UTILS_HPP

#include <cctype>
#include <vector>
#include <string_view>

namespace draft
{
    inline bool little_endian()
    {
        uint16_t one = 0x0001;

        return *((char*)&one);
    }

    inline constexpr uint64_t reverse(uint64_t n)
    {
        return ((n >> 56) | ((n & 0x00ff000000000000) >> 40) | ((n & 0x0000ff0000000000) >> 24) |
               ((n & 0x000000ff00000000) >> 8) | ((n & 0x00000000ff000000) << 8)  |
               ((n & 0x0000000000ff0000) << 24) | ((n & 0x000000000000ff00) << 40) | ((n << 56)));
    }

    inline constexpr uint64_t hton64(uint64_t n)
    {
        return little_endian() ? draft::reverse(n) : n;
    }

    inline uint64_t ntoh64(uint64_t n)
    {
        return hton64(n);
    }

    std::string str_tolower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });

        return s;
    }

    std::string str_toupper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });

        return s;
    }

    template <typename T>
    std::size_t replace_all(T& s, std::string_view what, std::string_view with)
    {
        std::size_t next = 0;
        std::size_t count = 0;

        while ((next = s.find(what.data(), next, what.length())) != std::string::npos)
        {
             s.replace(next, what.length(), with.data(), with.length());

             ++count;
             next += with.length();
        }

        return count;
    }

    template <typename T>
    std::vector<T> split(const T& src, const std::type_identity_t<T>& sep)
    {
        size_t curr = 0;
        size_t next = 0;

        std::vector<T> v;

        while ((curr = src.find_first_of(sep, next)) != std::string::npos)
        {
            v.emplace_back(std::string(src.begin() + next, src.begin() + curr));
            next = curr + 1;
        }

        v.emplace_back(v.empty() ? src : src.substr(next));

        return v;
    }
}

#endif
