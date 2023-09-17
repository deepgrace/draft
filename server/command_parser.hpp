//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef COMMAND_PARSER_HPP
#define COMMAND_PARSER_HPP

#include <tuple>
#include <string>

namespace kv
{
    enum command_type
    {
        get,
        set,
        del,
        unknown
    };

    enum result_type
    {
        good,
        bad,
        need_more
    };

    struct command
    {
        void reset()
        {
            key.clear();
            value.clear();

            type = unknown;
        }

        std::string key;
        std::string value;

        command_type type;
    };

    class command_parser
    {
    public:
        enum state
        {
            start,
            type_g,
            type_ge,
            type_s,
            type_se,
            type_set,
            type_d,
            type_de,
            type_done,
            read_key,
            read_set_key,
            read_set_blank,
            read_set_value,
            carriage
        };

        void reset()
        {
            state_ = start;
        }

        template <typename T>
        std::tuple<result_type, T> parse(command& cmd, T begin, T end)
        {
            result_type result = need_more;

            while (begin != end)
            {
                result = do_parse(cmd, *begin++);

                if (result != need_more)
                    return std::make_tuple(result, begin);
            }

            return std::make_tuple(result, end);
        }

    private:
        result_type do_parse(command& cmd, char c)
        {
            switch (state_)
            {
            case start:
                if (isblank(c))
                    return need_more;
                else if (c == 'g')
                {
                    state_ = type_g;

                    return need_more;
                }
                else if (c == 's')
                {
                    state_ = type_s;

                    return need_more;
                }
                else if (c == 'd')
                {
                    state_ = type_d;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_g:
                if (c == 'e')
                {
                    state_ = type_ge;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_ge:
                if (c == 't')
                {
                    cmd.type = get;
                    state_ = type_done;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_s:
                if (c == 'e')
                {
                    state_ = type_se;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_se:
                if (c == 't')
                {
                    cmd.type = set;
                    state_ = type_set;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_set:
                if (isblank(c))
                    return need_more;
                else
                {
                    cmd.key.push_back(c);
                    state_ = read_set_key;

                    return need_more;
                }
            break;

            case type_d:
                if (c == 'e')
                {
                    state_ = type_de;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_de:
                if (c == 'l')
                {
                    cmd.type = del;
                    state_ = type_done;

                    return need_more;
                }
                else
                    return bad;
            break;

            case type_done:
                if (c == '\r' || c == '\n')
                    return bad;
                else if (isblank(c))
                    return need_more;
                else
                {
                    cmd.key.push_back(c);
                    state_ = read_key;

                    return need_more;
                }
            break;

            case read_set_key:
                if (c == '\r' || c == '\n')
                    return bad;
                else if (isblank(c))
                {
                    state_ = read_set_blank;

                    return need_more;
                }
                else
                {
                    cmd.key.push_back(c);

                    return need_more;
                }
            break;

            case read_set_blank:
                if (c == '\r' || c == '\n')
                    return bad;
                else if (isblank(c))
                    return need_more;
                else
                {
                    cmd.value.push_back(c);
                    state_ = read_set_value;

                    return need_more;
                }
            break;

            case read_set_value:
                if (c == '\r')
                {
                    state_ = carriage;

                    return need_more;
                }
                else
                {
                    cmd.value.push_back(c);

                    return need_more;
                }
            break;

            case read_key:
                if (c == '\r')
                {
                    state_ = carriage;

                    return need_more;
                }
                else
                {
                    cmd.key.push_back(c);

                    return need_more;
                }
            break;

            case carriage:
                if (c == '\n')
                    return good;
                else
                    return bad;
            break;
            }

            return bad;
        }

        state state_;
    };
}

#endif
