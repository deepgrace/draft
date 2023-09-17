#!/bin/bash

if (( ${#} == 0 )); then
      ./clean.sh
fi

bin=raft_server

if [[ ! -f ${bin} ]]; then
      echo "${0}: cannot access '${bin}': No such file"
      exit 1
fi

./${bin} 127.0.0.1 9093 raft.conf
