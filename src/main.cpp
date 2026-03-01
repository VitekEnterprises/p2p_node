#include "node.hpp"
#include <iostream>
#include <csignal>
#include <memory>

std::unique_ptr<p2p::P2PNode> g_node;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\nShutting down..." << std::endl;
        if (g_node) {
            g_node->stop();
        }
        exit(0);
    }
}

void printHelp() {
    std::cout << "P2P Node Commands:\n"
              << "  /help           - Show this help\n"
              << "  /status         - Show node status\n"
              << "  /nodes          - Show routing table size\n"
              << "  /share <file>   - Share a file\n"
              << "  /download <hash> - Download a file\n"
              << "  /exit           - Exit\n";
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    uint16_t port = 6881;
    std::string storagePath = "./storage";
    
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) storagePath = argv[2];
    
    try {
        auto node = std::make_unique<p2p::P2PNode>(port, storagePath);
        g_node = std::move(node);
        
        if (!g_node->init()) {
            std::cerr << "Failed to initialize node" << std::endl;
            return 1;
        }
        
        // Bootstrap nodes
        std::vector<std::string> bootstrapNodes = {
            "127.0.0.1:6882"
        };
        
        g_node->bootstrap(bootstrapNodes);
        g_node->start();
        
        std::cout << "\n[*] P2P Node running on port " << port << std::endl;
        std::cout << "[*] Node ID: " << p2p::SHA256::toHex(g_node->getNodeId()) << std::endl;
        std::cout << "[*] Storage path: " << storagePath << std::endl;
        printHelp();
        
        // Interactive console
        std::string line;
        while (true) {
            std::cout << "> ";
            std::getline(std::cin, line);
            
            if (line == "/exit") {
                break;
            } else if (line == "/help") {
                printHelp();
            } else if (line == "/status") {
                std::cout << "Node is running. Total nodes in routing table: " 
                         << g_node->getRoutingTable().totalNodes() << std::endl;
            } else if (line == "/nodes") {
                std::cout << "Routing table size: " 
                         << g_node->getRoutingTable().totalNodes() << std::endl;
            } else if (line.substr(0, 7) == "/share ") {
                std::string filepath = line.substr(7);
                if (g_node->shareFile(filepath)) {
                    std::cout << "File shared successfully" << std::endl;
                } else {
                    std::cout << "Failed to share file" << std::endl;
                }
            } else if (line.substr(0, 10) == "/download ") {
                std::string hashStr = line.substr(10);
                auto hash = p2p::SHA256::fromHex(hashStr);
                g_node->downloadFile(hash, "./downloads");
            } else if (!line.empty()) {
                std::cout << "Unknown command. Type /help for help." << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    if (g_node) {
        g_node->stop();
    }
    
    return 0;
}
