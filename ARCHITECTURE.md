# P2P Node Architecture

A comprehensive diagram and description of all components of a P2P node.

## Component Hierarchy

'''

┌─────────────────────────────────────────┐

│ P2PNode (main.cpp) │

│ ├─ UDPSocket (network) │

│ ├─ DHTProtocol (dht) │

│ │ └─ RoutingTable (dht) │

│ │ └─ KBuckets (dht) │

│ ├─ HolePuncher (nat) │

│ └─ BlockStore (storage) │

└─────────────────────────────────────────┘

'''

## Data Flows

### 1. Node Initialization

'''

main()

↓

P2PNode::init()

├─ Random::init()

├─ UDPSocket::init() → bind to port

├─ Load/create node.id

└─ Setup packet handler

P2PNode::start()

├─ UDPSocket::startReceiveLoop() → thread

└─ maintenanceLoop() → thread

'''

### 2. Bootstrap - Joining the Network

'''

bootstrap(["node1:port1", "node2:port2"])

↓

Sends PING/WHOAMI to each bootstrap node

↓

Receives YOURADDR (discovers its public IP)

↓

Sends FIND_NODE with a random ID

↓

Receives FOUND_NODES with the nearest nodes

↓

Adds nodes to RoutingTable

'''

### 3. Packet Reception

'''

UDPSocket::receiveLoop() [thread]

↓ recv()

PacketSerializer::parse()

↓

DHTProtocol::handlePacket()

├─ Update routing table

└─ Dispatch to handler

├─ handlePing() → PONG

├─ handleFindNode() → FOUND_NODES

├─ handleStore() → STORED

└─ ...

'''

### 4. File Sharing

'''

shareFile("path/file.txt")

↓

BlockStore::storeFile()

├─ Loads the file

├─ Splits into 32KB blocks

├─ SHA256 hash of each block

├─ Saves blocks: /storage/blocks/{hash}

└─ Saves metadata: /storage/metadata/{fileHash}.meta

↓

Sends STORE to k-nearest nodes

(k=8 - from routing table)

'''

### 5. Finding and Downloading a File

'''

downloadFile(fileHash)

↓

DHTProtocol::findValue(fileHash)

↓

Finds 3 nearest nodes from routing table

↓

Sends FIND_VALUE to these nodes

↓ (parallel)

Nodes return FOUND_NODES (or content directly)

↓

RequestBlock() → MSG_REQUEST_BLOCK

↓

Requests blocks from holders

↓ (parallel)

Downloads blocks

↓

BlockStore::loadFile()

├─ Loads metadata

├─ Collects all blocks

└─ Constructs the original file

'''

## Network Protocol

### Message Types

'''

0x01: MSG_PING

0x02: MSG_PONG

0x03: MSG_FIND_NODE // Find nearest nodes

0x04: MSG_FOUND_NODES // Return nearest nodes

0x05: MSG_FIND_VALUE // Find data by hash

0x06: MSG_FOUND_VALUE // Return data

0x07: MSG_STORE // Store data

0x08: MSG_STORED // Store confirmation

0x09: MSG_WHOAMI // Request public address

0x0A: MSG_YOURADDR // Return public address

0x0B: MSG_HOLE_PUNCH // NAT hole punch request

0x0C: MSG_REQUEST_BLOCK // Request file block

0x0D: MSG_SEND_BLOCK // Send file block

'''

### Packet Structure

'''

┌──────────────────────────────────────────┐

│ Type (1B) | SenderID (32B) | Payload (*) │

└──────────────────────────────────────────┘

Example FOUND_NODES payload:

┌────────────────────────────────────────┐

│ Count (2B) | Node1 | Node2 | ... NodeN │

└────────────────────────────────────────┘

Where Node = ID(32B) | IP(4B) | Port(2B)

'''

## Storage Structure

'''

storage/

├── blocks/

│ ├── {hash1} (32KB block data)

│ ├── {hash2}

│ └── {hashN}

├── metadata/

│ ├── {fileHash1}.meta

│ ├── {fileHash2}.meta

│ └── {fileHashN}.meta

└── files/

├── downloaded_file1.txt

├── downloaded_file2.pdf

└── ...

'''

### Metadata Format

'''

Binary format:

┌─────────────────────────────────────┐

│ NameLen (2B) │

│ Filename (NameLen bytes) │

│ FileSize (8B) │

│ TotalBlocks (4B) │

│ BlockHash1 (32B) │

│ BlockHash2 (32B) │

│ ... │

│ BlockHashN (32B) │

└─────────────────────────────────────┘

'''

## Routing Table (Kademlia DHT)

### Structure

'''

256 K-Buckets (256 bit NodeID)

├─ KBucket 0 (furthest: bit 255)

├─ KBucket 1

├─ ...

└─ KBucket 255 (nearest: bit 0)

Each KBucket contains max K=8 NodeInfo

'''

### XOR Distance Metric

'''cpp

distance(A, B) = A XOR B

Properties:

- Symmetric: distance(A,B) = distance(B,A)

- Transitive: distance(A,B) < distance(A,C) ⟹ B closer to C

- Self-proof: distance(A,A) = 0

'''

### Node Lookup (Iterative Find)

'''

findClosest(target, k=8)

↓

1. Start with k nearest nodes from routing table

2. Contact them: FIND_NODE(target)

3. Get their responses with new nodes

4. Filter duplicates

5. Sort by XOR distance to target

6. Repeat until the result doesn't change or timeout

7. Return k nearest

'''

## Cross-Platform Implementation

### Windows vs Linux Detection

'''cpp

#ifdef _WIN32

// Windows-specific code

#include

SOCKET sock = socket(...);

ioctlsocket(sock, FIONBIO, &mode); // non-blocking

#else

// POSIX-specific code

#include

int sock = socket(...);

fcntl(sock, F_SETFL, O_NONBLOCK); // non-blocking

#endif

'''

### Platform Differences

| Aspect | Windows | Linux |

|--------|---------|-------|

| Socket type | 'SOCKET' (unsigned) | 'int' |

| Bad socket | 'INVALID_SOCKET' | '-1' |

| Close | 'closesocket()' | 'close()' |

| Non-blocking | 'ioctlsocket()' | 'fcntl()' |

| Select 1st arg | '0' | 'nfds+1' |

| Error codes | 'WSAGetLastError()' | 'errno' |

## Threading Model

'''

main thread

├─ User input loop (blocking stdin)

│

├─ receive_thread (UDPSocket)

│ └─ select() + recvfrom() loop

│ └─ packet_handler callback

│

└─ maintenance_thread

└─ Bucket refresh (each 15 min)

'''

## Mutual Exclusion (Mutexes)

'''

RoutingTable → KBucket → std::mutex

NodeInfo ← add/find/remove

UDPSocket → sendMutex_

→ packet processing

BlockStore → file I/O (single-threaded OK)

'''

## Performance Characteristics

### Time Complexity

- **Lookup**: O(log N) hops, ~20 for millions of nodes

- **Add node**: O(1) amortized

- **Find closest**: O(log N) amortized

- **Distance calculation**: O(32) bytes = O(1)

### Space Complexity

- **Per node**: ~256 KB-buckets × 8 nodes = 2000 NodeInfo structs

- **NodeInfo size**: ~70 bytes → ~140 KB routing table

- **Block cache**: Configurable, default 0 (disk only)

## Improvements for Production

1. **RPC ID Tracking**

- Using queryId for matching responses

2. **Alpha Parameter** (ALPHA = 3)

- Contact the alpha nearest nodes in parallel

- Improves lookup to log N / log ALPHA

3. **Bucket Refresh**

- Automatic refreshes after N hours

- Publishing own ID

4. **Persistence**

- Saving the routing table to disk

- Speeds up bootstrap

5. **Security**

- Sybil attack prevention

- IP-based Node-ID verification

- Rate limiting

The current implementation is a functional MVP without these optimizations.