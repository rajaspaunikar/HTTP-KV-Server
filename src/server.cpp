#include "../include/httplib.h"
#include "../include/lru_cache.h"
#include <pqxx/pqxx>
#include <thread>
#include <optional>
#include "../include/json.hpp"
#include "../include/logger.h"

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
    log_event("Creating new database connection");
    return pqxx::connection(DB_CONNECTION_STRING);
}

// CREATE operation
bool db_create(const std::string& key, const std::string& value) {
    log_event("DB CREATE: Attempting to insert/update key '" + key + "' with value length " + std::to_string(value.length()));
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::work txn(conn);
        
        txn.exec(
            "INSERT INTO kv_store (key, value) VALUES ($1, $2) "
            "ON CONFLICT (key) DO UPDATE SET value = $2", pqxx::params{key,value});
            
        txn.commit();
        log_event("DB CREATE: Successfully committed key '" + key + "'");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "DB Create Error: " << e.what() << std::endl;
        log_event("DB CREATE: Failed for key '" + key + "' due to exception");
        return false;
    }
}

// READ operation
std::optional<std::string> db_read(const std::string& key) {
    log_event("DB READ: Fetching key '" + key + "' from database");
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::nontransaction ntxn(conn);
        
        pqxx::result res = ntxn.exec("SELECT value FROM kv_store WHERE key = $1", pqxx::params{key});
        
        if (res.empty()) {
            log_event("DB READ: Key '" + key + "' not found in database");
            return std::nullopt; // Not found
        }
        std::string value = res[0][0].as<std::string>();
        log_event("DB READ: Successfully fetched key '" + key + "' (value length: " + std::to_string(value.length()) + ")");
        return value;
    } catch (const std::exception& e) {
        std::cerr << "DB Read Error: " << e.what() << std::endl;
        log_event("DB READ: Failed for key '" + key + "' due to exception");
        return std::nullopt;
    }
}

// DELETE operation
bool db_delete(const std::string& key) {
    log_event("DB DELETE: Attempting to delete key '" + key + "' from database");
    try {
        pqxx::connection conn = create_db_connection();
        pqxx::work txn(conn);
        
        pqxx::result res = txn.exec("DELETE FROM kv_store WHERE key = $1", pqxx::params{key});
        txn.commit();
        bool deleted = res.affected_rows() > 0;
        if (deleted) {
            log_event("DB DELETE: Successfully deleted key '" + key + "'");
        } else {
            log_event("DB DELETE: Key '" + key + "' not found (no rows affected)");
        }
        return deleted;
    } catch (const std::exception& e) {
        std::cerr << "DB Delete Error: " << e.what() << std::endl;
        log_event("DB DELETE: Failed for key '" + key + "' due to exception");
        return false;
    }
}


// --- Main Server ---
int main() {
    log_event("Server startup: Initializing with " + std::to_string(SERVER_THREAD_COUNT) + " threads on port " + std::to_string(SERVER_PORT));
    httplib::Server svr;

    // Set a thread pool for the server
    svr.new_task_queue = [] { 
        return new httplib::ThreadPool(SERVER_THREAD_COUNT); 
    };

    log_event("Server startup: Connecting to database...");
    // Test database connection on startup
    try {
        create_db_connection();
        log_event("Server startup: Database connection successful");
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Database connection failed: " << e.what() << std::endl;
        log_event("Server startup: FATAL - Database connection failed");
        return 1;
    }

    log_event("Server startup: Setting up RESTful endpoints");

    // === RESTful Endpoints ===

    // 1. CREATE (POST /kv)
    // Body: {"key": "my_key", "value": "my_value"}
    svr.Post("/kv", [](const httplib::Request& req, httplib::Response& res) {
        log_event("HTTP REQUEST: POST /kv - Body length: " + std::to_string(req.body.length()) + ", Headers: " + std::to_string(req.headers.size()));
        json j;
        try {
            j = json::parse(req.body);
        } catch (...) {
            log_event("HTTP REQUEST: POST /kv - Invalid JSON in body");
            res.status = 400; // Bad Request
            res.set_content("{\"error\":\"Invalid JSON format\"}", "application/json");
            return;
        }

        if (!j.contains("key") || !j.contains("value")) {
            log_event("HTTP REQUEST: POST /kv - Missing 'key' or 'value' in JSON");
            res.status = 400; // Bad Request
            res.set_content("{\"error\":\"Missing 'key' or 'value'\"}", "application/json");
            return;
        }

        std::string key = j["key"];
        std::string value = j["value"];
        log_event("HTTP REQUEST: POST /kv - Parsed key: '" + key + "', value length: " + std::to_string(value.length()));

        // 1. Store in database
        if (db_create(key, value)) {
            // 2. Store in cache
            log_event("CACHE: Putting key '" + key + "' into LRU cache");
            cache.put(key, value);
            log_event("HTTP RESPONSE: POST /kv - Created successfully for key '" + key + "'");
            res.status = 201; // Created
            res.set_content("{\"status\":\"created\", \"key\":\"" + key + "\"}", "application/json");
        } else {
            log_event("HTTP RESPONSE: POST /kv - Failed to create key '" + key + "'");
            res.status = 500; // Internal Server Error
            res.set_content("{\"error\":\"Failed to write to database\"}", "application/json");
        }
    });

    // 2. READ (GET /kv/<key>)
    svr.Get(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];
        log_event("HTTP REQUEST: GET /kv/" + key + " - Headers: " + std::to_string(req.headers.size()));

        // 1. Check cache
        log_event("CACHE: Attempting get for key '" + key + "'");
        auto cache_val = cache.get(key);
        if (cache_val) {
            // Cache Hit
            log_event("CACHE: HIT for key '" + key + "' (value length: " + std::to_string(cache_val->length()) + ")");
            json j_res = {{"key", key}, {"value", *cache_val}, {"source", "cache"}};
            res.set_content(j_res.dump(), "application/json");
            log_event("HTTP RESPONSE: GET /kv/" + key + " - Served from cache");
            return;
        }
        log_event("CACHE: MISS for key '" + key + "'");

        // 2. Cache Miss: Fetch from database
        auto db_val = db_read(key);
        if (db_val) {
            // 3. Insert into cache
            log_event("CACHE: Putting key '" + key + "' into LRU cache after DB fetch");
            cache.put(key, *db_val);
            json j_res = {{"key", key}, {"value", *db_val}, {"source", "database"}};
            res.set_content(j_res.dump(), "application/json");
            log_event("HTTP RESPONSE: GET /kv/" + key + " - Served from database and cached");
        } else {
            log_event("HTTP RESPONSE: GET /kv/" + key + " - Key not found");
            res.status = 404; // Not Found
            res.set_content("{\"error\":\"Key not found\", \"key\":\"" + key + "\"}", "application/json");
        }
    });

    // 3. DELETE (DELETE /kv/<key>)
    svr.Delete(R"(/kv/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];
        log_event("HTTP REQUEST: DELETE /kv/" + key + " - Headers: " + std::to_string(req.headers.size()));

        // 1. Delete from database
        if (db_delete(key)) {
            // 2. Delete from cache
            log_event("CACHE: Removing key '" + key + "' from LRU cache");
            cache.remove(key);
            log_event("HTTP RESPONSE: DELETE /kv/" + key + " - Deleted successfully");
            res.status = 200;
            res.set_content("{\"status\":\"deleted\", \"key\":\"" + key + "\"}", "application/json");
        } else {
            log_event("HTTP RESPONSE: DELETE /kv/" + key + " - Delete failed (not found or error)");
            res.status = 404;
            res.set_content("{\"error\":\"Key not found or delete failed\", \"key\":\"" + key + "\"}", "application/json");
        }
    });

    log_event("Server startup: All endpoints registered, starting listener on 0.0.0.0:" + std::to_string(SERVER_PORT));
    // Start listening
    svr.listen("0.0.0.0", SERVER_PORT);
    log_event("Server shutdown: Listener stopped");
    return 0;
}