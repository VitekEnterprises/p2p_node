# P2P Node - Distributed Network (DHT)

Implementation of a peer-to-peer node with support for Kademlia DHT, NAT hole punching and distributed file sharing. Fully cross-platform (Windows + Linux).

## Main Goal

The main goal of this project is to build communication that can also work remotely over radio waves.

## Features

- **Kademlia DHT core** - node discovery, routing table, FIND_NODE/FIND_VALUE/STORE
- **UDP transport** - custom binary protocol, non-blocking socket, broadcast bootstrap
- **File sharing** - metadata-based sharing + block transfer for larger files
- **Adaptive block storage** - block size 1-8 KB based on file size
- **User registry** - hardwareID -> nickname propagation and persistence (`users.dat`)
- **Hash synchronization** - known hash exchange and local persistence (`hashes.dat`)
- **Decentralized websites** - publish/list/open websites (`/makeweb`, `/websites`, `/web`)
- **P2P chat** - network message broadcast (`/msg`)
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
│ ├── storage/
│ │ └── block_store.hpp # Block store
│ └── web/
│   ├── basic_browser.hpp/cpp # Native/basic browser window fallback
│   ├── file_store.hpp/cpp # Website folder/file helpers
│   ├── html_parser.hpp/cpp # Lightweight HTML -> text renderer
│   ├── web_manager.hpp/cpp # Website identity, metadata, sync logic
│   └── website_metadata.hpp/cpp # Website metadata serialization/signature
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
cmake ..
cmake --build .
```

The resulting binary will be in `build/bin/p2p_node`

## Usage

Node startup flow:

1. Initialize UDP socket and node identity.
2. Discover peers automatically via interface broadcast addresses + `255.255.255.255` fallback.
3. If no peer replies, prompt for manual bootstrap `ip:port`.
4. Start periodic sync loops (hashes, users, websites).

File sharing behavior:

- `/share <file> [public|private]` stores file into content blocks and creates metadata.
- The file hash is printed and can be used with `/download <hash>`.
- Public shares are announced to peers; private shares remain hash-addressed only.
- Downloaded/shared/hash catalogs persist to binary files in the storage directory.

Large-file transfer behavior:

- Metadata is stored in DHT (filename, filesize, total blocks, block size, block hashes).
- Receiver requests blocks using `MSG_REQUEST_BLOCK` / `MSG_SEND_BLOCK`.
- Block size is adaptive (`1 KB`-`8 KB`) depending on file size.

User identity behavior:

- Hardware ID is derived from system identifiers (with fallback strategy).
- Nickname registry is synced across peers and persisted in obfuscated `users.dat`.


```bash
# Run with default port 6881
./p2p_node

# Or with specified port and storage path
./p2p_node 6882 ./my_storage
```

### Commands

```
/help                 - Show command help
/status               - Show node status
/nodes                - Show routing table size
/hashes               - Show known hashes with download stats
/downloaded           - Show downloaded files (`downloaded.dat`)
/shared               - Show locally shared files (`shared.dat`)
/users                - Show known hardwareID -> nickname mappings (`users.dat`)
/makeweb <path>       - Publish decentralized website from folder (requires index.html)
/websites             - List known decentralized websites
/web <domain>         - Open decentralized website by domain
/find <hash>          - Find online owners of a hash (currently limited/placeholder)
/share <file> [public|private] - Share a file (default: private)
/download <hash>      - Download file by SHA-256 hash
/msg <text>           - Broadcast chat message
/exit                 - Exit program
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
- **Binary protocol** - compact custom packet format
- **Message Types (24)**:
	- Core DHT: `PING`, `PONG`, `FIND_NODE`, `FOUND_NODES`, `FIND_VALUE`, `FOUND_VALUE`, `STORE`, `STORED`
	- Addressing/NAT: `WHOAMI`, `YOURADDR`, `HOLE_PUNCH`
	- File blocks: `REQUEST_BLOCK`, `SEND_BLOCK`, `SHARE_ANNOUNCE`
	- Social/sync: `NICK`, `CHAT`, `HASH_LIST_REQUEST`, `HASH_LIST`, `USER_REGISTRY_REQUEST`, `USER_REGISTRY`, `USER_ANNOUNCE`
	- Website sync: `WEBSITE_LIST`, `WEBSITE_REQUEST`, `WEBSITE_METADATA`

### Block storage

- Adaptive block size (`1 KB`-`8 KB`)
- SHA256 block hashes + file hash
- Metadata persisted in `.meta` files
- Automatic file split/reconstruction

### Website subsystem

- Node-local website identity (`identity.dat`: public/private key pair)
- Signed metadata exchange (`websites.dat` + website messages)
- Deterministic metadata conflict resolution
- Open website by domain using local render + basic browser fallback

### Persistent local data

- `node.id` - stable node identifier
- `hashes.dat` - known file hashes
- `downloaded.dat` - downloaded file map
- `shared.dat` - shared file map
- `users.dat` - obfuscated hardwareID/nickname registry
- `identity.dat` - website identity
- `websites.dat` - known website metadata

## Future improvements

- [ ] Enable remote communication over radio waves (main project goal)
- [ ] Complete `/find <hash>` owner lookup (currently placeholder path)
- [ ] TCP fallback for larger packets
- [ ] Iterative search (ALPHA = 3)
- [ ] Bucket refresh after timeout
- [ ] RPC ID tracking
- [ ] Persistence routing tables
- [ ] Communication encryption
- [ ] Bootstrap file discovery

## License

MIT License