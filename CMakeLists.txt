#
# Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/deepgrace/draft
#

cmake_minimum_required(VERSION 3.22)
project(DRAFT)
 
add_subdirectory(raft)
add_subdirectory(client)
add_subdirectory(server)
