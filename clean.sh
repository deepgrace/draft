#!/bin/bash

path=$(pwd -P)

for d in client config server; do
    cd ${path}/${d}
    ./clean.sh
done

for d in a b c; do
    cd ${path}/cluster/${d}

    ./clean.sh
    rm -f raft_server
done

cd ${path}
rm -rf bin build
