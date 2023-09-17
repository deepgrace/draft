# draft [![LICENSE](https://img.shields.io/github/license/deepgrace/draft.svg)](https://github.com/deepgrace/draft/blob/main/LICENSE_1_0.txt) [![Language](https://img.shields.io/badge/language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support) [![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://github.com/deepgrace/draft)

> **Distributed, Reliable, Replicated, Redundant And Fault Tolerant**

## Introduction
draft is a C++ library used for distributed system development, which is header-only, extensible and modern C++ oriented.  
It's built on top off the **arpc** and protobuf, it's based on the **Proactor** design pattern with performance in mind.  
draft enables you to develop scalable and distributed system in a straightforward, asynchronous and OOP manner.  

draft is mainly consist of five parts:
- **raft** The implementation of the raft consensus algorithm
- **config** The configuration programs to manage the cluster
- **client** The client to connect to a server in the cluster
- **server** The snapshot based kv storage built on top off raft
- **cluster** The distributed kv storage system consist of three servers

draft provides the following features:
- **Data transfer**
- **Leader election**
- **Log compaction**
- **Log replication**
- **Leadership transfer**
- **Membership changes**
- **Persistence**

## Prerequsites
[arpc](https://github.com/deepgrace/arpc)  
[protobuf](https://github.com/protocolbuffers/protobuf)  

## Compiler requirements
The library relies on a C++20 compiler and standard library

More specifically, draft requires a compiler/standard library supporting the following C++20 features (non-exhaustively):
- concepts
- lambda templates
- All the C++20 type traits from the <type_traits> header

## Building
draft is header-only. To use it just add the necessary `#include` line to your source files, like this:
```cpp
#include <draft.hpp>
```

`git clone https://github.com/deepgrace/arpc.git` and place it with draft under the same directory.  
To build the project with cmake, `cd` to the root of the project and setup the build directory:
```bash
mkdir build
cd build
cmake ..
```

Make and install the executables:
```
make -j4
make install
```

The executables are now located at the `config`, `client`, `server` and `cluster` directories of the root of the project.  
The project can also be built with the script `build.sh`, just run it, the executables will be put at the corresponding 
directories.  

Note: the `config_server` executable will be put at the `/tmp` directory, as a standalone program, it is just a show case.

## Cleaning
```bash
./clean.sh
```

## Running
`cd cluster`, open three terminals, then run `start.sh` in directory `a`, `b` and `c` respectively (a is default the leader).  
The `start.sh` script accepts a argument of any kind to indicate whether to start a fresh run or load the last saved states.  
Default is the former, for a reload start, just run `./start.sh 1`, which will load all the data since the server last shutdown.

## Connecting
`cd client`, `./connect.sh <a | b | c>`, after the connection has been established, it's time to enter commands.

## Supported commands
`set key value`  
`get key`  
`del key`  

## Management of the cluster
`cd config`  

show servers in the cluster:  
`./show_servers.sh <a | b | c>`  

show server states in the cluster:  
`./show_states.sh <a | b | c>`  

add `b`, `c` to the cluster:  
`./add_servers.sh a b c`  

transfer leadership from `a` to `b`:  
`./transfer.sh a b`  

del `a`, `c` from the cluster:  
`./del_servers.sh b a c`  

append log to the cluster:  
`./append.sh b "set metaprogramming amazing"`  

read or write the log content of a server:  
`./read_entry.sh <a | b | c>`  

then enter commands, the `read_entry.sh` script supports the following commands:  
`exit`  
`help`  
`last`  
`show`  
`append <log>`  
`chop <index>`  
`drop <index>`  

The `read_entry.sh` should only run when the server is stopped.  

## Configuration
The configure file `raft.conf` under directory `a`, `b` and `c` of `cluster` defined a variety of information needed to 
bootstrap the server smoothly, it's well commented and self explained.  

To add a new server to the cluster, say `d`, just copy it from `c`, modify the `host` and `port` parts in `raft.conf`, 
modify the listen address in `start.sh`, then add that address to `address.sh` under directory `config`, that's all.
 
## Logging
The server is currently using the Boost.Log as its logger, the logs to print is adjustable by a log threshold, which is 
defined in the variable `SEVERITY_THRESHOLD` in file `severity_logger.hpp` under the `raft` directory.

## Storage
The storage service implemented in server is a memory based STL map, the server uses the memory based snapshotting 
approach to do data compaction.  

A more efficient and incremental approaches to data compaction is to deploy a dedicated storage engine as its backend.  

It's pretty easy to do, just replace the type `database_t` defined in file `storage_server.hpp` 
under the `server` directory with the storage engine you prefered, the remaining modifications need to do will be 
kept minimal.  

The raft implementation has its own persistence and log compaction mechanisms, it's independent of the server.

## License
draft is licensed as [Boost Software License 1.0](LICENSE_1_0.txt).
