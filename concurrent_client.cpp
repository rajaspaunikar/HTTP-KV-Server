#include <httplib.h>
#include <thread>
#include <vector>
#include <iostream>

using namespace httplib;

void make_request(int id) {
    Client client("127.0.0.1", 8080);
    auto res = client.Get("/");
    if (res) {
        std::cout << "Client " << id << " received: " << res->body << "\n";
    } else {
        std::cout << "Client " << id << " failed to connect\n";
    }
}

int main() {
    const int num_clients = 1000;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back(make_request, i);
    }

    for (auto &t : threads) t.join();

    return 0;
}
