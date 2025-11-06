#pragma once
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <memory>
#include <string>

class KVDatabase {
    std::unique_ptr<sql::mysql::MySQL_Driver> driver;
    std::unique_ptr<sql::Connection> conn;

public:
    KVDatabase(const std::string& host, const std::string& user,
               const std::string& pass, const std::string& db) {
        driver.reset(sql::mysql::get_mysql_driver_instance());
        conn.reset(driver->connect(host, user, pass));
        conn->setSchema(db);

        auto stmt = conn->createStatement();
        stmt->execute(
            "CREATE TABLE IF NOT EXISTS kv_store ("
            "  `key` VARCHAR(255) PRIMARY KEY,"
            "  `value` TEXT NOT NULL"
            ") ENGINE=InnoDB;"
        );
        delete stmt;
    }

    bool put(const std::string& key, const std::string& value) {
        auto pstmt = conn->prepareStatement(
            "INSERT INTO kv_store (`key`, `value`) VALUES (?, ?) "
            "ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)"
        );
        pstmt->setString(1, key);
        pstmt->setString(2, value);
        bool res = pstmt->execute();
        delete pstmt;
        return res;
    }

    std::optional<std::string> get(const std::string& key) {
        auto pstmt = conn->prepareStatement("SELECT `value` FROM kv_store WHERE `key` = ?");
        pstmt->setString(1, key);
        auto res = pstmt->executeQuery();
        if (res->next()) {
            std::string val = res->getString(1);
            delete res;
            delete pstmt;
            return val;
        }
        delete res;
        delete pstmt;
        return std::nullopt;
    }

    bool remove(const std::string& key) {
        auto pstmt = conn->prepareStatement("DELETE FROM kv_store WHERE `key` = ?");
        pstmt->setString(1, key);
        bool res = pstmt->executeUpdate() > 0;
        delete pstmt;
        return res;
    }
};