#!/bin/bash

declare -A leaders
declare -A servers
declare -A listens
declare -A statmap
declare -A ldbsmap

# http address used by administrator to config the cluster
leaders["a"]=127.0.0.1:7071
leaders["b"]=127.0.0.1:7072
leaders["c"]=127.0.0.1:7073

# rpc address used by raft to send rpc messages
servers["a"]=127.0.0.1:8081
servers["b"]=127.0.0.1:8082
servers["c"]=127.0.0.1:8083

# listen address used by raft server to accept client connections
listens["a"]=127.0.0.1:9091
listens["b"]=127.0.0.1:9092
listens["c"]=127.0.0.1:9093

file_path()
{
    local path=../cluster/${1}

    awk -F'=' -v f=${2} -v p=${path} '$0 ~ f {
        if (substr($2, 0, 1) == "/")
        {
            print $2
        }
        else
        {
            print p"/"$2
        };

        exit
    }' ${path}/raft.conf
}

# stat file path
statmap["a"]=$(file_path a stat_file)
statmap["b"]=$(file_path b stat_file)
statmap["c"]=$(file_path c stat_file)

# ldbs file path
ldbsmap["a"]=$(file_path a ldbs_file)
ldbsmap["b"]=$(file_path b ldbs_file)
ldbsmap["c"]=$(file_path c ldbs_file)
