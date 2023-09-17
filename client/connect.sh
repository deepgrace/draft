#!/bin/bash

if (( ${#} != 1 )); then
      echo "Usage  : ${0} server"
      echo "example: ${0} <a | b | c>"

      exit 1
fi

source ../config/address.sh
listen="${listens[${1}]}"

if [[ "${listen}" == "" ]]; then
      echo "${1} doesn't exist"
      exit 1
fi

echo connected to ${listen}
./raft_client ${listen/:/ }
