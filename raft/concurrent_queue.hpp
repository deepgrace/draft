//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef CONCURRENT_QUEUE_HPP
#define CONCURRENT_QUEUE_HPP

#include <mutex>
#include <queue>
#include <condition_variable>

namespace draft
{
    template <typename T>
    class concurrent_queue
    {
    public:
        T pop()
        {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [&]{ return !que.empty(); });

            auto t = que.front();
            que.pop();

            return t;
        }

        void pop(T& t)
        {
            std::unique_lock<std::mutex> lock(mut);
            cond.wait(lock, [&]{ return !que.empty(); });

            t = que.front();
            que.pop();
        }

        bool try_pop(T& t)
        {
            std::unique_lock<std::mutex> lock(mut);

            if (que.empty())
                return false;

            t = que.front();
            que.pop();

            return true;
        }

        void push(const T& t)
        {
            {
                std::unique_lock<std::mutex> lock(mut);
                que.push(t);
            }

            cond.notify_one();
        }

        void push(T&& t)
        {
            {
                std::unique_lock<std::mutex> lock(mut);
                que.push(std::move(t));
            }

            cond.notify_one();
        }

        size_t size()
        {
            return que.size();
        }

        bool empty() const
        {
            return que.empty();
        }

        bool empty()
        {
            std::unique_lock<std::mutex> lock(mut);

            return que.empty();
        }

    private:
        std::mutex mut;
        std::queue<T> que;

        std::condition_variable cond;
    };
}

#endif
