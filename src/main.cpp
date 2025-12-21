#include "exchange_simulator.h"
#include <iostream>
#include <cstdlib>
#include <iomanip>
#include <sstream>

int main(int argc, char* argv[]) {
    // Parse command-line arguments (simple version)
    uint64_t seed = 12345;
    uint64_t max_events = 1000;
    
    if (argc > 1) {
        seed = std::stoull(argv[1]);
    }
    if (argc > 2) {
        max_events = std::stoull(argv[2]);
    }
    
    exchange::SimulatorConfig config;
    config.seed = seed;
    config.enable_udp_streaming = false;
    config.enable_replay_mode = false;
    
    exchange::ExchangeSimulator simulator;
    simulator.configure(config);
    
    if (!simulator.initialize()) {
        std::cerr << "Failed to initialize simulator" << std::endl;
        return EXIT_FAILURE;
    }
    
    std::cout << "Starting simulation with seed: " << seed << std::endl;
    std::cout << "Max events: " << max_events << std::endl;
    
    // Run simulation (single call - run() handles the loop internally)
    simulator.run();
    
    simulator.shutdown();
    
    std::cout << "\nSimulation complete." << std::endl;
    
    // Print event log summary
    simulator.print_event_log_summary();
    
    // Export price data to CSV for visualization
    std::stringstream csv_filename;
    csv_filename << "data/price_data_seed_" << seed << ".csv";
    simulator.export_price_data_to_csv(csv_filename.str());
    
    return EXIT_SUCCESS;
}

