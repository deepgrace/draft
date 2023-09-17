#!/bin/bash

g++ -std=c++23 -Wall -O3 -Os -s -I . raft_client.cpp -o raft_client

strip raft_client
