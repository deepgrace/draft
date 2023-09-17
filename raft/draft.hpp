//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef DRAFT_HPP
#define DRAFT_HPP

#include <raft_base.hpp>
#include <raft_impl.hpp>
#include <raft_rpc.hpp>

namespace draft
{
    using raft = raft_impl<rpc_service_impl>;
}

#endif
