#!/bin/bash

path=$(pwd -P)

cd ../raft
protoc --cpp_out=. raft.proto

cd ${path}

g++ -std=c++23 -Wall -O3 -Os -s -I . -I ../raft -I ../../arpc/include -D BOOST_LOG_DYN_LINK \
-l protobuf -l pthread -l boost_thread -l boost_log_setup -l boost_log -l boost_program_options \
../raft/raft.pb.cc raft_server.cpp -o raft_server

strip raft_server

for d in a b c; do
    cp raft_server ../cluster/${d}
done

cd ../raft
./clean.sh
