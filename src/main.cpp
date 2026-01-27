#include "Exchange.h"

#include <iostream>
#include <cstdlib>
#include <cstring>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n"
              << "\nOptions:\n"
              << "  --help, -h              Show this help message\n"
              << "  --port <port>           ZMQ ROUTER port for order entry (default: 12345)\n"
              << "  --mcast-group <ip>      Multicast group address (default: 239.255.0.1)\n"
              << "  --mcast-port <port>     Multicast port (default: 12346)\n"
              << "  --symbol <symbol>       Trading symbol (default: SYM1)\n"
              << "  --dummy                 Enable dummy order generator\n"
              << "  --dummy-rate <n>        Dummy orders per second (default: 10)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ExchangeConfiguration config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            config.zmqPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--mcast-group" && i + 1 < argc) {
            config.multicastGroup = argv[++i];
        } else if (arg == "--mcast-port" && i + 1 < argc) {
            config.multicastPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--symbol" && i + 1 < argc) {
            config.symbol = argv[++i];
        } else if (arg == "--dummy") {
            config.enableDummyGenerator = true;
        } else if (arg == "--dummy-rate" && i + 1 < argc) {
            config.dummyConfig.ordersPerSecond = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Configure dummy generator defaults
    config.dummyConfig.basePrice = 1000;
    config.dummyConfig.priceRange = 50;
    config.dummyConfig.tickSize = 1;
    config.dummyConfig.minQuantity = 1;
    config.dummyConfig.maxQuantity = 100;
    config.dummyConfig.buyRatio = 0.5;
    config.dummyConfig.limitRatio = 0.8;
    config.dummyConfig.marketRatio = 0.1;
    config.dummyConfig.iocRatio = 0.1;
    
    // Create and start exchange
    Exchange exchange(config);
    
    if (!exchange.start()) {
        std::cerr << "Failed to start exchange" << std::endl;
        return 1;
    }
    
    // Wait for shutdown signal
    exchange.waitForShutdown();
    
    return 0;
}
