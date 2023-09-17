#!/bin/bash

path=$(pwd -P)

for d in raft client server; do
    cd ${path}/${d}
    ./build.sh
done
