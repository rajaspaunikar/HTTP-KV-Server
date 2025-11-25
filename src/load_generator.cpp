#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <iomanip>
#include <mutex>

#include "../include/httplib.h"
#include "../include/json.hpp"   

using json = nlohmann::json;
using namespace std::chrono;

// --- Configuration Constants ---
const std::string SERVER_HOST = "localhost";
const int SERVER_PORT = 8080;

// Make payload size configurable here
const size_t PAYLOAD_BYTES = 4096; // 4 KB payloads; increase if you want more IO

// --- Workload Types ---
enum WorkloadType {
    PUT_ALL = 1,      // Write heavy (Disk bound)
    GET_ALL = 2,      // Read heavy unique keys (Cache Miss -> Disk bound)
    GET_POPULAR = 3,  // Read heavy small set of keys (Cache Hit -> CPU/Mem bound)
    MIXED = 4         // Random mix
};

// --- Per-Thread Statistics ---
struct ThreadStats {
    long long request_count = 0;
    long long total_latency_us = 0; // Microseconds
    long long errors = 0;
};

// --- Random Helper ---
// Generates a random string for values
std::string random_string(size_t length) {
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    thread_local std::mt19937 rng((std::random_device())());
    thread_local std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s += charset[dist(rng)];
    }
    return s;
}

// --- Worker Function ---
// Note: now takes thread_id for unique key naming
void worker_thread(int thread_id, int duration_sec, WorkloadType workload, ThreadStats& stats) {
    // Each thread gets its own client to simulate a distinct user
    httplib::Client cli(SERVER_HOST, SERVER_PORT);

    // Set timeouts suitable for load testing (seconds)
    cli.set_connection_timeout(2); 
    cli.set_read_timeout(5);
    cli.set_write_timeout(5);

    // Random Number Generators
    std::random_device rd;
    std::mt19937 gen(rd());

    // Ranges
    // For "Get All" we want a huge range to ensure cache misses.
    std::uniform_int_distribution<> dist_huge(1, 10000000); 
    // For "Get Popular" we want a range matching cache capacity (1..100)
    std::uniform_int_distribution<> dist_small(1, 100); 
    
    std::uniform_int_distribution<> dist_mixed_op(0, 100); // For mixed percentages

    uint64_t put_counter = 0; // per-thread counter for uniq keys

    auto start_time = high_resolution_clock::now();
    auto end_time = start_time + seconds(duration_sec);

    while (high_resolution_clock::now() < end_time) {
        std::string path;
        int key_int = 0;
        std::string key_str;
        bool is_post = false;
        bool is_delete = false;

        // Determine Operation Type
        switch (workload) {
            case PUT_ALL:
                is_post = true;
                key_int = dist_huge(gen);
                break;
            case GET_ALL:
                key_int = dist_huge(gen); 
                break;
            case GET_POPULAR:
                key_int = dist_small(gen);
                break;
            case MIXED: {
                int op = dist_mixed_op(gen);
                if (op < 10) is_delete = true;
                else if (op < 50) is_post = true;
                // Mixed mostly uses large keyspace to generate varied behavior
                key_int = dist_huge(gen);
                break;
            }
            default:
                key_int = dist_huge(gen);
        }

        auto req_start = high_resolution_clock::now();
        httplib::Result res;

        if (is_post) {
            // Use unique keys for PUTs to maximize new-object disk writes
            ++put_counter;
            key_str = "key_t" + std::to_string(thread_id) + "_" + std::to_string(put_counter);

            json j;
            j["key"] = key_str;
            j["value"] = random_string(PAYLOAD_BYTES); // bigger payload

            // Request headers: content-type + optional flush hint
            httplib::Headers headers = {
                {"Content-Type", "application/json"},
                {"X-Force-Flush", "1"} // server can choose to honor this to fsync
            };

            res = cli.Post("/kv", headers, j.dump(), "application/json");
        } else if (is_delete) {
            key_str = "key_" + std::to_string(key_int);
            path = "/kv/" + key_str;
            res = cli.Delete(path.c_str());
        } else {
            // GET
            key_str = "key_" + std::to_string(key_int);
            path = "/kv/" + key_str;
            res = cli.Get(path.c_str());
        }

        auto req_end = high_resolution_clock::now();

        // 3. Record Stats
        stats.request_count++;
        auto latency = duration_cast<microseconds>(req_end - req_start).count();
        stats.total_latency_us += latency;

        if (!res || (res->status >= 500)) {
            // Count network errors or server crashes
            stats.errors++;
        }
        // Note: 404 is not counted as an error (valid miss)
    }
}

// --- Pre-population Helper ---
// For GET_POPULAR: prepopulate keys 1..100 so cache-hot set matches capacity
void prepopulate_popular_keys() {
    std::cout << "[Setup] Pre-populating 100 popular keys for Cache Hit workload..." << std::endl;
    httplib::Client cli(SERVER_HOST, SERVER_PORT);
    for (int i = 1; i <= 100; ++i) {
        std::string key = "key_" + std::to_string(i);
        json j = { {"key", key}, {"value", std::string("prepopulated_value_") + std::to_string(i)} };
        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"X-Force-Flush", "1"}
        };
        auto res = cli.Post("/kv", headers, j.dump(), "application/json");
        if (!res || res->status >= 500) {
            std::cerr << "[Setup] Warning: prepopulate POST failed for " << key << std::endl;
        }
    }
    std::cout << "[Setup] Done." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <num_threads> <duration_seconds> <workload_type>" << std::endl;
        std::cerr << "Workloads: 1=PutAll, 2=GetAll(Miss), 3=GetPopular(Hit), 4=Mixed" << std::endl;
        return 1;
    }

    int num_threads = std::stoi(argv[1]);
    int duration = std::stoi(argv[2]);
    int workload_input = std::stoi(argv[3]);
    
    if (workload_input < 1 || workload_input > 4) {
        std::cerr << "Invalid workload type." << std::endl;
        return 1;
    }
    WorkloadType workload = static_cast<WorkloadType>(workload_input);

    // Special setup for Get Popular to ensure hits
    if (workload == GET_POPULAR) {
        prepopulate_popular_keys();
    }

    std::cout << "------------------------------------------------" << std::endl;
    std::cout << "Starting Load Test" << std::endl;
    std::cout << "Threads: " << num_threads << " | Duration: " << duration << "s | Workload: " << workload_input << std::endl;
    std::cout << "Payload per PUT: " << PAYLOAD_BYTES << " bytes" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    std::vector<std::thread> threads;
    std::vector<ThreadStats> all_stats(num_threads);

    // Start Threads (pass thread id as first arg)
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread, i /*thread id*/, duration, workload, std::ref(all_stats[i]));
    }

    // Wait for Threads
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // Aggregation
    long long total_reqs = 0;
    long long total_latency_us = 0;
    long long total_errors = 0;

    for (const auto& s : all_stats) {
        total_reqs += s.request_count;
        total_latency_us += s.total_latency_us;
        total_errors += s.errors;
    }

    double avg_throughput = (double)total_reqs / duration;
    double avg_response_time_ms = 0.0;
    if (total_reqs > 0) {
        avg_response_time_ms = ((double)total_latency_us / total_reqs) / 1000.0;
    }

    // Output Metrics
    std::cout << std::endl;
    std::cout << "=== Performance Results ===" << std::endl;
    std::cout << "Total Requests:      " << total_reqs << std::endl;
    std::cout << "Successful Requests: " << (total_reqs - total_errors) << std::endl;
    std::cout << "Errors:              " << total_errors << std::endl;
    std::cout << "---------------------------" << std::endl;
    std::cout << "Average Throughput:    " << std::fixed << std::setprecision(2) << avg_throughput << " req/sec" << std::endl;
    std::cout << "Average Response Time: " << std::fixed << std::setprecision(3) << avg_response_time_ms << " ms" << std::endl;
    std::cout << "===========================" << std::endl;

    return 0;
}
