#!/bin/bash

rm -f raft.logs

awk -F'=' '/^path/ || /_file/ {print $2}' raft.conf | xargs -i rm -f {}
