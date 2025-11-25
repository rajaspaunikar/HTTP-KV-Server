#include "../include/httplib.h"
#include "../include/lru_cache.h"
#include <pqxx/pqxx>
#include <thread>
#include <optional>
#include <memory> // <--- CHANGED: Added for smart pointers (unique_ptr)
#include "../include/json.hpp"
#include "../include/logger.h"

// --- Configuration ---
const int SERVER_PORT = 8080;
const int CACHE_CAPACITY = 100; 
const int SERVER_THREAD_COUNT = 16; 
const std::string DB_CONNECTION_STRING = "dbname=kv_system user=kv_user password=password host=localhost sslmode=require";

// <--- CHANGED: Global toggle to disable logging. 
// Set this to false for Load Testing to save CPU. Set to true for debugging.
const bool ENABLE_LOGGING = false; 
// ---------------------

using json = nlohmann::json;

// Global cache instance
LRUCache cache(CACHE_CAPACITY);

// <--- CHANGED: Wrapper to stop string building when logging is disabled
void log_debug(const std::string& message) {
    if (ENABLE_LOGGING) {
        log_event(message);
    }
}

// --- Database Operations ---

// <--- CHANGED: Replaces old create_db_connection()
// This keeps one connection open per thread (Thread Local Storage)
// preventing the overhead of TCP handshakes on every request.
pqxx::connection& get_db_connection() {
    // Each thread gets its own unique pointer to a connection
    thread_local std::unique_ptr<pqxx::connection> connection;

    // If connection doesn't exist or is closed, create a new one
    if (!connection || !connection->is_open()) {
        log_debug("Opening new Thread-Local Database Connection");
        try {
            connection = std::make_unique<pqxx::connection>(DB_CONNECTION_STRING);

            // <--- CHANGED: Prepare statements once when connection opens.
            // This makes SQL execution much faster.
            connection->prepare("insert_kv", 
                "INSERT INTO kv_store (key, value) VALUES ($1, $2) "
                "ON CONFLICT (key) DO UPDATE SET value = $2");
            connection->prepare("select_kv", "SELECT value FROM kv_store WHERE key = $1");
            connection->prepare("delete_kv", "DELETE FROM kv_store WHERE key = $1");

        } catch (const std::exception& e) {
            std::cerr << "FATAL: DB Connection failed: " << e.what() << std::endl;
            throw; // Re-throw to be caught by caller
        }
    }
    return *connection;
}

// CREATE operation
bool db_create(const std::string& key, const std::string& value) {
    // log_debug calls are now cheap because of the boolean check
    log_debug("DB CREATE: " + key); 
    try {
        // <--- CHANGED: Get existing thread-local connection
        pqxx::connection& conn = get_db_connection();
        pqxx::work txn(conn);
        
        // <--- CHANGED: Use exec_prepared instead of exec
        txn.exec_prepared("insert_kv", key, value);
            
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        // If the DB goes down, we print error but don't crash
        std::cerr << "DB Create Error: " << e.what() << std::endl;
        return false;
    }
}

// READ operation
std::optional<std::string> db_read(const std::string& key) {
    log_debug("DB READ: " + key);
    try {
        pqxx::connection& conn = get_db_connection();
        pqxx::nontransaction ntxn(conn);
        
        // <--- CHANGED: Use exec_prepared
        pqxx::result res = ntxn.exec_prepared("select_kv", key);
        
        if (res.empty()) {
            return std::nullopt; 
        }
        return res[0][0].as<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "DB Read Error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

// DELETE operation
bool db_delete(const std::string& key) {
    log_debug("DB DELETE: " + key);
    try {
        pqxx::connection& conn = get_db_connection();
        pqxx::work txn(conn);
        
        // <--- CHANGED: Use exec_prepared
        pqxx::result res = txn.exec_prepared("delete_kv", key);
        txn.commit();
        return res.affected_rows() > 0;
    } catch (const std::exception& e) {
        std::cerr << "DB Delete Error: " << e.what() << std::endl;
        return false;
    }
}


// --- Main Server ---
int main() {
    std::cout << "Server starting on port " << SERVER_PORT << "..." << std::endl;
    std::cout << "Logging Enabled: " << (ENABLE_LOGGING ? "YES" : "NO") << std::endl;

    httplib::Server svr;

    svr.new_task_queue = [] { 
        return new httplib::ThreadPool(SERVER_THREAD_COUNT); 
    };

    // Test initial connection (Optional, just to fail fast if DB is down)
    try {
        get_db_connection(); 
        std::cout << "Initial DB Check Passed." << std::endl;
    } catch (...) {
        return 1;
    }

    // === RESTful Endpoints ===

    // 1. CREATE (POST /kv)
    svr.Post("/kv", [](const httplib::Request& req, httplib::Response& res) {
        // Logging removed or wrapped
        log_debug("POST /kv");
        
        json j;
        try {
            j = json::parse(req.body);
        } catch (...) {
            res.status = 400; 
            return;
        }

        if (!j.contains("key") || !j.contains("value")) {
            res.status = 400; 
            return;
        }

        std::string key = j["key"];
        std::string value = j["value"];

        // 1. Store in database
        if (db_create(key, value)) {
            // 2. Store in cache
            cache.put(key, value);
            res.status = 201; 
            // <--- CHANGED: Minimized response body to save bandwidth/CPU
            res.set_content("{\"status\":\"ok\"}", "application/json");
        } else {
            res.status = 500; 
        }
    });

    // 2. READ (GET /kv/<key>)
    svr.Get(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];
        log_debug("GET /kv/" + key);

        // 1. Check cache
        auto cache_val = cache.get(key);
        if (cache_val) {
            json j_res = {{"key", key}, {"value", *cache_val}, {"src", "mem"}}; // shortened JSON
            res.set_content(j_res.dump(), "application/json");
            return;
        }

        // 2. Cache Miss: Fetch from database
        auto db_val = db_read(key);
        if (db_val) {
            cache.put(key, *db_val);
            json j_res = {{"key", key}, {"value", *db_val}, {"src", "db"}};
            res.set_content(j_res.dump(), "application/json");
        } else {
            res.status = 404; 
            res.set_content("{}", "application/json");
        }
    });

    // 3. DELETE (DELETE /kv/<key>)
    svr.Delete(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];
        log_debug("DELETE /kv/" + key);

        if (db_delete(key)) {
            cache.remove(key);
            res.status = 200;
            res.set_content("{\"status\":\"del\"}", "application/json");
        } else {
            res.status = 404;
        }
    });

    svr.listen("0.0.0.0", SERVER_PORT);
    return 0;
}