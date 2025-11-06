#include "httplib.h"
#include "cache.hpp"
#include "db.hpp"
#include "thread_pool.hpp"
#include <iostream>
#include <chrono>
#include <random>

const int CACHE_SIZE = 1000;
const int THREAD_POOL_SIZE = 8;

LRUCache<std::string, std::string> cache(CACHE_SIZE);
KVDatabase db("tcp://127.0.0.1:3306", "root", "password", "kvdb");
ThreadPool pool(THREAD_POOL_SIZE);

std::string generate_value() {
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<> dist(100, 1000);
    return "value_" + std::to_string(dist(gen));
}

int main() {
    httplib::Server svr;

    svr.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
    });

    // CREATE / PUT
    svr.Put("/kv/(.+)", [&](const httplib::Request& req, httplib::Response& res) {
        pool.enqueue([&, key = req.matches[1].str()] {
            auto value = req.has_param("v") ? req.get_param_value("v") : generate_value();
            cache.put(key, value);
            db.put(key, value);
            res.set_content("OK", "text/plain");
        });
    });

    // READ
    svr.Get("/kv/(.+)", [&](const httplib::Request& req, httplib::Response& res) {
        pool.enqueue([&, key = req.matches[1].str()] {
            auto cached = cache.get(key);
            if (cached) {
                res.set_content(*cached, "text/plain");
                return;
            }
            auto val = db.get(key);
            if (val) {
                cache.put(key, *val);
                res.set_content(*val, "text/plain");
            } else {
                res.status = 404;
                res.set_content("Not Found", "text/plain");
            }
        });
    });

    // DELETE
    svr.Delete("/kv/(.+)", [&](const httplib::Request& req, httplib::Response& res) {
        pool.enqueue([&, key = req.matches[1].str()] {
            db.remove(key);
            cache.remove(key);
            res.set_content("OK", "text/plain");
        });
    });

    std::cout << "Server starting on :8080\n";
    svr.listen("0.0.0.0", 8080);
}