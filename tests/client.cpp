#include "../include/httplib.h"
#include "../include/json.hpp"
#include <iostream>
#include <string>
#include <cstdlib>

using json = nlohmann::json;

int main() {
    const std::string SERVER_HOST = "http://localhost:8080";
    httplib::Client cli(SERVER_HOST);

    std::cout << "KV Store Client Connected to " << SERVER_HOST << std::endl;
    std::cout << "Enter commands in an infinite loop. Type 'quit' to exit." << std::endl;

    while (true) {
        std::cout << "\n--- Menu ---" << std::endl;
        std::cout << "1. POST (Create: Enter key and value)" << std::endl;
        std::cout << "2. GET (Read: Enter key)" << std::endl;
        std::cout << "3. DELETE (Delete: Enter key)" << std::endl;
        std::cout << "Type 'quit' to exit." << std::endl;
        std::cout << "Your choice: ";

        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "quit" || choice == "q") {
            std::cout << "Exiting client." << std::endl;
            break;
        }

        int option;
        try {
            option = std::stoi(choice);
        } catch (...) {
            std::cout << "Invalid input. Please enter 1, 2, 3, or 'quit'." << std::endl;
            continue;
        }

        if (option == 1) {
            // POST /kv
            std::cout << "Enter key: ";
            std::string key;
            std::getline(std::cin, key);

            std::cout << "Enter value: ";
            std::string value;
            std::getline(std::cin, value);

            json j = {{"key", key}, {"value", value}};
            std::string json_str = j.dump();

            auto res = cli.Post("/kv", json_str, "application/json");

            if (res && res->status == 201) {
                std::cout << "SUCCESS (201): " << res->body << std::endl;
            } else if (res) {
                std::cout << "Response (" << res->status << "): " << res->body << std::endl;
            } else {
                std::cout << "ERROR: Failed to connect to server or send request." << std::endl;
            }

        } else if (option == 2) {
            // GET /kv/<key>
            std::cout << "Enter key: ";
            std::string key;
            std::getline(std::cin, key);

            auto res = cli.Get("/kv/" + key);

            if (res && (res->status == 200 || res->status == 404)) {
                std::cout << "Response (" << res->status << "): " << res->body << std::endl;
            } else if (res) {
                std::cout << "Unexpected Response (" << res->status << "): " << res->body << std::endl;
            } else {
                std::cout << "ERROR: Failed to connect to server or send request." << std::endl;
            }

        } else if (option == 3) {
            // DELETE /kv/<key>
            std::cout << "Enter key: ";
            std::string key;
            std::getline(std::cin, key);

            auto res = cli.Delete("/kv/" + key);

            if (res && (res->status == 200 || res->status == 404)) {
                std::cout << "Response (" << res->status << "): " << res->body << std::endl;
            } else if (res) {
                std::cout << "Unexpected Response (" << res->status << "): " << res->body << std::endl;
            } else {
                std::cout << "ERROR: Failed to connect to server or send request." << std::endl;
            }

        } else {
            std::cout << "Invalid option. Please enter 1, 2, 3, or 'quit'." << std::endl;
        }
    }

    return 0;
}