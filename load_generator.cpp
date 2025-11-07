#include "httplib.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include "json.hpp"


// --- Configuration ---
std::string SERVER_HOST = "127.0.0.1";
int SERVER_PORT = 8080;
int NUM_THREADS = 8;
int DURATION_SECONDS = 30; // 5 minutes (300s) is long, use 30s for quick tests
std::string WORKLOAD_TYPE = "get_popular"; // "put_all", "get_all", "get_popular", "get_put"
// ---------------------

using json = nlohmann::json; // **FIXED:** Added using directive

// Shared atomic counters
std::atomic<long long> total_requests(0);
std::atomic<long long> total_response_time_ms(0);
std::atomic<bool> keep_running(true);

// For 'get_popular' workload
const int POPULAR_KEYS_COUNT = 50;


// --- Worker Thread Function ---
void client_worker(int thread_id) {
    // Each thread gets its own HTTP client and random number generator
    httplib::Client cli(SERVER_HOST, SERVER_PORT);
    cli.set_connection_timeout(5, 0); // 5-second timeout
    cli.set_read_timeout(5, 0);

    // Seed the random generator uniquely for each thread
    std::mt19937 gen(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::uniform_int_distribution<> popular_dist(0, POPULAR_KEYS_COUNT - 1);
    std::uniform_int_distribution<> random_dist(0, 1000000);
    std::uniform_int_distribution<> mix_dist(0, 9); // For 70% get, 30% put

    long thread_req_count = 0;

    while (keep_running) {
        std::string key;
        std::string value;
        std::string body;
        httplib::Result res;
        json j;

        // Start timer
        auto start = std::chrono::steady_clock::now();

        try {
            if (WORKLOAD_TYPE == "put_all") {
                key = "key_t" + std::to_string(thread_id) + "_" + std::to_string(thread_req_count);
                value = "val_" + std::to_string(random_dist(gen));
                j = {{"key", key}, {"value", value}};
                res = cli.Post("/kv", j.dump(), "application/json");

            } else if (WORKLOAD_TYPE == "get_all") {
                // Generates unique keys per thread to force cache misses
                key = "key_t" + std::to_string(thread_id) + "_" + std::to_string(thread_req_count);
                res = cli.Get("/kv/" + httplib::detail::encode_url(key));

            } else if (WORKLOAD_TYPE == "get_popular") {
                // Accesses one of K popular keys
                key = "popular_key_" + std::to_string(popular_dist(gen));
                res = cli.Get("/kv/" + httplib::detail::encode_url(key));

            } else if (WORKLOAD_TYPE == "get_put") {
                int op = mix_dist(gen);
                if (op < 7) { // 70% GET (popular)
                    key = "popular_key_" + std::to_string(popular_dist(gen));
                    res = cli.Get("/kv/" + httplib::detail::encode_url(key));
                } else { // 30% PUT
                    key = "key_t" + std::to_string(thread_id) + "_" + std::to_string(thread_req_count);
                    value = "val_" + std::to_string(random_dist(gen));
                    j = {{"key", key}, {"value", value}};
                    res = cli.Post("/kv", j.dump(), "application/json");
                }
            } else {
                // Should not happen
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // End timer
            auto end = std::chrono::steady_clock::now();

            // Check response and update counters
            // We count 404 (Not Found) as a "successful" round trip
            if (res && (res->status == 200 || res->status == 201 || res->status == 404)) {
                total_requests++;
                total_response_time_ms += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            } else if (res) {
                 // std::cerr << "HTTP Error: " << res->status << std::endl;
            } else {
                 // std::cerr << "Request failed: " << httplib::to_string(res.error()) << std::endl;
            }

        } catch (const std::exception& e) {
            // std::cerr << "Exception in worker thread: " << e.what() << std::endl;
        }

        thread_req_count++;
        // Closed-loop: This thread waits for the response before sending the next request.
    }
}

// --- Main Function ---
int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: ./load_gen <host> <port> <threads> <duration_sec> <workload_type>" << std::endl;
        std::cerr << "Workload types: put_all, get_all, get_popular, get_put" << std::endl;
        return 1;
    }

    SERVER_HOST = argv[1];
    SERVER_PORT = std::stoi(argv[2]);
    NUM_THREADS = std::stoi(argv[3]);
    DURATION_SECONDS = std::stoi(argv[4]);
    WORKLOAD_TYPE = argv[5];

    std::cout << "Starting load generator..." << std::endl;
    std::cout << "  Target: " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
    std::cout << "  Threads: " << NUM_THREADS << std::endl;
    std::cout << "  Duration: " << DURATION_SECONDS << " seconds" << std::endl;
    std::cout << "  Workload: " << WORKLOAD_TYPE << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    // Launch worker threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(client_worker, i);
    }

    // Run for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(DURATION_SECONDS));

    // Stop all threads
    keep_running = false;

    // Wait for all threads to finish
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "\n--- Load test finished ---" << std::endl;

    // Calculate and display metrics
    double duration_actual = DURATION_SECONDS;
    long long total_req = total_requests.load();
    long long total_time = total_response_time_ms.load();

    double avg_throughput = (total_req > 0) ? (total_req / duration_actual) : 0;
    double avg_response_time = (total_req > 0) ? (static_cast<double>(total_time) / total_req) : 0;

    std::cout << "Total Requests: " << total_req << std::endl;
    std::cout << "Total Test Time: " << duration_actual << " s" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    std::cout << "Average Throughput: " << avg_throughput << " req/sec" << std::endl;
    std::cout << "Average Response Time: " << avg_response_time << " ms" << std::endl;

    return 0;
}