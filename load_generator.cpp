#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

struct Stats {
    std::atomic<uint64_t> success{0}, fail{0}, total_time_us{0};
};

void client_thread(int id, int duration_sec, const std::string& workload,
                   const std::string& server_ip, Stats& stats) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return;
    }

    std::mt19937 gen(id);
    std::uniform_int_distribution<> key_dist(1, 10000);
    std::uniform_int_distribution<> op_dist(0, 99);
    std::vector<std::string> popular_keys = {"key1", "key2", "key3", "key4", "key5"};

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= duration_sec)
            break;

        std::string key, method, path;
        int op = op_dist(gen);

        if (workload == "put_all") {
            key = "key" + std::to_string(key_dist(gen));
            method = "PUT";
            path = "/kv/" + key + "?v=val" + std::to_string(gen());
        } else if (workload == "get_all") {
            key = "key" + std::to_string(key_dist(gen));
            method = "GET";
            path = "/kv/" + key;
        } else if (workload == "get_popular") {
            key = popular_keys[gen() % popular_keys.size()];
            method = "GET";
            path = "/kv/" + key;
        } else { // mixed
            if (op < 40) { // 40% put
                key = "key" + std::to_string(key_dist(gen));
                method = "PUT";
                path = "/kv/" + key + "?v=val" + std::to_string(gen());
            } else if (op < 95) { // 55% get
                key = (op < 80) ? popular_keys[gen() % popular_keys.size()]
                               : "key" + std::to_string(key_dist(gen));
                method = "GET";
                path = "/kv/" + key;
            } else { // 5% delete
                key = "key" + std::to_string(key_dist(gen));
                method = "DELETE";
                path = "/kv/" + key;
            }
        }

        std::string request = method + " " + path + " HTTP/1.1\r\n"
                              "Host: " + server_ip + "\r\n"
                              "Connection: keep-alive\r\n\r\n";

        auto t1 = std::chrono::high_resolution_clock::now();
        if (send(sock, request.c_str(), request.size(), 0) <= 0) {
            stats.fail++;
            continue;
        }

        char buffer[4096];
        int n = recv(sock, buffer, sizeof(buffer)-1, 0);
        auto t2 = std::chrono::high_resolution_clock::now();

        if (n > 0) {
            buffer[n] = '\0';
            stats.success++;
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
            stats.total_time_us += us;
        } else {
            stats.fail++;
        }
    }
    close(sock);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cout << "Usage: ./loadgen <server_ip> <threads> <duration_sec> <workload>\n";
        std::cout << "workload: put_all | get_all | get_popular | mixed\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int threads = std::stoi(argv[2]);
    int duration = std::stoi(argv[3]);
    std::string workload = argv[4];

    Stats stats;
    std::vector<std::thread> clients;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < threads; ++i) {
        clients.emplace_back(client_thread, i, duration, workload, server_ip, std::ref(stats));
    }

    for (auto& t : clients) t.join();

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    double throughput = stats.success / elapsed;
    double avg_rt = stats.success > 0 ? (stats.total_time_us / stats.success) / 1000.0 : 0;

    std::cout << "\n=== Load Test Results ===\n";
    std::cout << "Workload: " << workload << "\n";
    std::cout << "Threads: " << threads << "\n";
    std::cout << "Duration: " << duration << "s\n";
    std::cout << "Throughput: " << throughput << " req/s\n";
    std::cout << "Avg Response Time: " << avg_rt << " ms\n";
    std::cout << "Success: " << stats.success << ", Fail: " << stats.fail << "\n";

    return 0;
}