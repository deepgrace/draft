#!/bin/bash

source address.sh

if (( ${#} < 2 )); then
      echo "Usage  : ${0} <leader> <server> [server] ..."
      echo "example: ${0} <a | b | c> <a | b | c> [a | b | c] ..."

      exit 1
fi

leader="${leaders[${1}]}"

if [[ "${leader}" == "" ]]; then
      echo "${1} doesn't exist"
      exit 1
fi

shift
server=()

for n in ${@}; do
    s="${servers[${n}]}"

    if [[ "${s}" == "" ]]; then
          echo "${1} doesn't exist"
          exit 1
    fi

    server=(${server[@]} ${s})
done

server="${server[@]}"
server=${server// /,}

echo leader ${leader}
echo server ${server}
