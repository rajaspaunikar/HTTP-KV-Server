#include "httplib.h"
#include "lru_cache.h"
#include <pqxx/pqxx>
#include <thread>
#include <iostream>
#include <optional>
#include "json.hpp"

// --- Configuration ---
const int SERVER_PORT = 8080;
const int CACHE_CAPACITY = 100; // Max items in cache
// Use std::thread::hardware_concurrency() or a fixed number
const int SERVER_THREAD_COUNT = 16; 
const std::string DB_CONNECTION_STRING = "dbname=kv_system user=kv_user password=password host=localhost sslmode=require";
// ---------------------

using json = nlohmann::json;

// Global cache instance
LRUCache cache(CACHE_CAPACITY);

// --- Database Operations ---

// Helper function to create a new DB connection
pqxx::connection create_db_connection() {
    return pqxx::connection(DB_CONNECTION_STRING);
}

// CREATE operation
bool db_create(const std::string& key, const std::string& value) {
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::work txn(conn);
        
        txn.exec(
            "INSERT INTO kv_store (key, value) VALUES ($1, $2) "
            "ON CONFLICT (key) DO UPDATE SET value = $2", pqxx::params{key,value});
            
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "DB Create Error: " << e.what() << std::endl;
        return false;
    }
}

// READ operation
std::optional<std::string> db_read(const std::string& key) {
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::nontransaction ntxn(conn);
        
        // **FIXED:** Use exec() instead of deprecated exec_params()
        pqxx::result res = ntxn.exec("SELECT value FROM kv_store WHERE key = $1", pqxx::params{key});
        
        if (res.empty()) {
            return std::nullopt; // Not found
        }
        return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "DB Read Error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

// DELETE operation
bool db_delete(const std::string& key) {
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::work txn(conn);
        
        pqxx::result res = txn.exec("DELETE FROM kv_store WHERE key = $1", pqxx::params{key});
        txn.commit();
        return res.affected_rows() > 0; // Return true if a row was actually deleted
    } catch (const std::exception& e) {
        std::cerr << "DB Delete Error: " << e.what() << std::endl;
        return false;
    }
}


// --- Main Server ---
int main() {
    httplib::Server svr;

    // Set a thread pool for the server
    svr.new_task_queue = [] { 
        return new httplib::ThreadPool(SERVER_THREAD_COUNT); 
    };

    std::cout << "Server starting with " << SERVER_THREAD_COUNT << " threads on port " << SERVER_PORT << "..." << std::endl;
    std::cout << "Connecting to database..." << std::endl;

    // Test database connection on startup
    try {
        create_db_connection();
        std::cout << "Database connection successful." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Database connection failed: " << e.what() << std::endl;
        return 1;
    }

    // === RESTful Endpoints ===

    // 1. CREATE (POST /kv)
    // Body: {"key": "my_key", "value": "my_value"}
    svr.Post("/kv", [](const httplib::Request& req, httplib::Response& res) {
        json j;
        try {
            j = json::parse(req.body);
        } catch (...) {
            res.status = 400; // Bad Request
            res.set_content("{\"error\":\"Invalid JSON format\"}", "application/json");
            return;
        }

        if (!j.contains("key") || !j.contains("value")) {
            res.status = 400; // Bad Request
            res.set_content("{\"error\":\"Missing 'key' or 'value'\"}", "application/json");
            return;
        }

        std::string key = j["key"];
        std::string value = j["value"];

        // 1. Store in database
        if (db_create(key, value)) {
            // 2. Store in cache
            cache.put(key, value);
            // **FIXED:** Use res.status instead of res.set_status
            res.status = 201; // Created
            res.set_content("{\"status\":\"created\", \"key\":\"" + key + "\"}", "application/json");
        } else {
            // **FIXED:** Use res.status instead of res.set_status
            res.status = 500; // Internal Server Error
            res.set_content("{\"error\":\"Failed to write to database\"}", "application/json");
        }
    });

    // 2. READ (GET /kv/<key>)
    svr.Get(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];

        // 1. Check cache
        auto cache_val = cache.get(key);
        if (cache_val) {
            // Cache Hit
            json j_res = {{"key", key}, {"value", *cache_val}, {"source", "cache"}};
            res.set_content(j_res.dump(), "application/json");
            return;
        }

        // 2. Cache Miss: Fetch from database
        auto db_val = db_read(key);
        if (db_val) {
            // 3. Insert into cache
            cache.put(key, *db_val);
            json j_res = {{"key", key}, {"value", *db_val}, {"source", "database"}};
            res.set_content(j_res.dump(), "application/json");
        } else {
            // **FIXED:** Use res.status instead of res.set_status
            res.status = 404; // Not Found
            res.set_content("{\"error\":\"Key not found\", \"key\":\"" + key + "\"}", "application/json");
        }
    });

    // 3. DELETE (DELETE /kv/<key>)
    svr.Delete(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];

        // 1. Delete from database
        if (db_delete(key)) {
            // 2. Delete from cache
            cache.remove(key);
            // **FIXED:** Use res.status instead of res.set_status
            res.status = 200;
            res.set_content("{\"status\":\"deleted\", \"key\":\"" + key + "\"}", "application/json");
        } else {
            // **FIXED:** Use res.status instead of res.set_status
            res.status = 404;
            res.set_content("{\"error\":\"Key not found or delete failed\", \"key\":\"" + key + "\"}", "application/json");
        }
    });

    // Start listening
    svr.listen("0.0.0.0", SERVER_PORT);
    return 0;
}