#include "exchange_simulator.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // TODO: Parse command-line arguments
    (void)argc;  // Unused for now
    (void)argv;  // Unused for now
    // For now, create a simple configuration
    
    exchange::SimulatorConfig config;
    config.seed = 12345;
    config.enable_udp_streaming = false;
    config.enable_replay_mode = false;
    
    exchange::ExchangeSimulator simulator;
    simulator.configure(config);
    
    if (!simulator.initialize()) {
        std::cerr << "Failed to initialize simulator" << std::endl;
        return EXIT_FAILURE;
    }
    
    simulator.run();
    simulator.shutdown();
    
    return EXIT_SUCCESS;
}

