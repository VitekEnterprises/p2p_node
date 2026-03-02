# P2P Node - Distributed Network (DHT)

Implementation of a peer-to-peer node with support for Kademlia DHT, NAT hole punching and distributed file sharing. Fully cross-platform (Windows + Linux).

## Features

- **DHT (Distributed Hash Table)** - Kademlia implementation
- **NAT Hole Punching** - Communication over NAT/firewall
- **File Sharing** - Distributed Block Storage
- **Cross-platform** - Windows and Linux

## Project Structure

```
p2p_node/
├── src/
│ ├── types.hpp # Basic types and cross-platform socket API
│ ├── main.cpp # Main program
│ ├── node.hpp # P2P node
│ ├── util/
│ │ ├── sha256.hpp/cpp # SHA256 hashing
│ │ └── random.hpp # Random number generation
│ ├── network/
│ │ ├── udp_socket.hpp/cpp # UDP socket (cross-platform)
│ │ └── packet_serializer.hpp # Packet serialization
│ ├── dht/
│ │ ├── xor_distance.hpp # XOR distance metric
│ │ ├── kbucket.hpp # K-bucket container
│ │ ├── routing_table.hpp # Routing table
│ │ └── dht_protocol.hpp # DHT protocol
│ ├── nat/
│ │ └── hole_punch.hpp # NAT hole punching
│ └── storage/
│ └── block_store.hpp # Block store
├── CMakeLists.txt # Build configuration
├── .gitignore
└── README.md
```

## Compilation

### Requirements

- **Windows**: Visual Studio 2019+ or MinGW with GCC 9+
- **Linux**: GCC 9+ or Clang 9+
- CMake 3.10+

### Compiling on Windows

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" # or another version of Visual Studio
cmake --build . --config Release
```

Or with MinGW:

```bash
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

### Compilation on Linux

Bootstrap node discovery: when run without a specified bootstrap address the program will enumerate your network interfaces, compute the correct broadcast address for each (based on IP and subnet mask) and send a UDP broadcast on port 6881. It also always sends one packet to the generic `255.255.255.255:6881` as a fallback. If no peers respond within a couple of seconds the node will ask you to enter a bootstrap address manually. You can also override discovery by providing a bootstrap address on the command line (see Usage below).

```bash
mkdir build
cd build
hmm..
cmake --build .
```

The resulting binary will be in `build/bin/p2p_node`

## Usage

When a file is shared publically the fact that you have the hash is propagated to peers but they will not see any filename or announcement printed in their console; private shares remain retrievable only by hash. Each node keeps a local catalog of known hashes (including files you upload or download) which it synchronises with other peers.  Known hashes are persisted to `hashes.dat` in the storage directory; downloaded and shared files are recorded in `downloaded.dat` and `shared.dat` respectively.  These files are binary and not meant to be edited by hand, but their contents can be inspected via the `/downloaded`, `/shared` and `/hashes` commands.

Large files (over about 64 KB) are transmitted using a simple block protocol rather than being sent in a single DHT packet, avoiding UDP size limits.  When you `/share` such a file it is split into 32 KB blocks and advertised; peers automatically request each block in turn, reconstructing the file locally.  

> **Download progress:** the console prints owner address, total blocks and a line for each block received.  After the final message `Large file downloaded to: <path>` the file will appear in `/downloaded`.  You may run `/downloaded` only once that message has appeared.

Nodes also maintain a **user registry** mapping hardware‑IDs to nicknames.  This registry is stored in `users.dat` (obfuscated) and is automatically merged with peers every minute.  When any peer learns a new (hwid,nick) pair it will be propagated through the network, dramatically increasing the chance that returning nodes are recognised and their nickname set correctly.


```bash
# Run with default port 6881
./p2p_node

# Or with specified port and storage path
./p2p_node 6882 ./my_storage
```

### Commands

Nodes remember the nicknames of peers they encounter; this information is stored locally in a binary file (`users.dat` in the storage directory) that is obfuscated so it is not human‑readable or easily modified. When you start the node it will ask all connected peers for their registries, and if any of them already know your hardware ID the returned nickname will be used automatically instead of the one saved locally.

> **Dynamic discovery:** nodes run a background sync every minute, asking all connected peers for their known
> hardware‑ID/nickname pairs and merging any results into the local `users.dat` store. Every (hwid, nickname) pair
> seen anywhere on the network is propagated this way, so each node gradually builds a complete directory. That
> maximises the chance that when a node rejoins, some peer already knows its ID and can provide the correct nickname
> automatically.


```
/help           - Show this help text
/status         - Node status
/nodes          - Number of nodes in routing table
/hashes         - List all known file hashes (see hashes.dat)
/downloaded     - Show downloaded files (persistent downloaded.dat)
/shared         - Show files you have shared (shared.dat)
/users          - Show known hardwareID→nickname mappings (users.dat)
/share <file> [public|private] - Share file (default private; public shares notify peers)
/download <hash> - Download file (specify SHA256 of shared file)
# The hash is printed when you or another node shares a file
/msg <text>   - Send chat message to peers
/exit           - Exit program
```

## Cross-platform implementation

The code automatically detects the operating system and uses the appropriate API:

- **Windows**: Winsock2 API
- **Linux/Unix**: Standard POSIX socket API

### Key cross-platform differences

1. **Header files** (`types.hpp`)
- Windows: `#include <winsock2.h>`
- Linux: POSIX socket headers

2. **Socket API**
- Windows: `SOCKET`, `INVALID_SOCKET`
- Linux: `int`, `-1`

3. **Non-blocking sockets**
- Windows: `ioctlsocket()` with `FIONBIO`
- Linux: `fcntl()` with `O_NONBLOCK`

4. **Select API**
- Windows: Special parameters for `select()`
- Linux: `fd_set` with `sockfd_t + 1`

## Architecture

### DHT Protocol (Kademlia)

- **XOR Distance**: Distance between nodes based on XOR operation
- **K-buckets**: Groups of 8 closest nodes
- **Routing Table**: 256 k-buckets based on bit distance
- **Operations**: PING, FIND_NODE, FIND_VALUE, STORE

### Communication

- **UDP** - Stateless, low latency
- **Binary Protocol** - Efficient serialization
- **Message Types**: 13 message types (PING, PONG, FIND_NODE, ...)

### Block storage

- 32 KB blocks
- SHA256 hash
- Metadata in JSON-like format
- Automatic file splitting

## Future improvements

- [ ] TCP fallback for larger packets
- [ ] Iterative search (ALPHA = 3)
- [ ] Bucket refresh after timeout
- [ ] RPC ID tracking
- [ ] Persistence routing tables
- [ ] Communication encryption
- [ ] Bootstrap file discovery

## License

MIT License