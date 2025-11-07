# HTTP KV Cache Server

This project implements a multi-threaded HTTP-based key-value (KV) store server in C++ with PostgreSQL as the persistent backend and an in-memory LRU cache for performance optimization. It supports RESTful CRUD operations (Create via POST, Read via GET, Delete via DELETE) and includes a closed-loop load generator for benchmarking throughput and latency under various workloads.

The system follows a client-server architecture with separated concerns: the server handles concurrent requests using a thread pool, caches frequent accesses in memory, and persists data to PostgreSQL. The load generator emulates multiple clients to stress-test the server, generating configurable workloads (e.g., read-heavy, write-heavy, mixed) without user input or file-based requests.

## Features
- **RESTful HTTP API**: Supports POST (create), GET (read), DELETE (delete) for KV pairs.
- **In-Memory LRU Cache**: Evicts least recently used items on overflow (capacity: 100 by default) to reduce database hits.
- **PostgreSQL Backend**: Persistent storage with ACID transactions for create/read/delete.
- **Multi-Threaded Server**: Uses a configurable thread pool (16 threads by default) for concurrency.
- **Load Generator**: Multi-threaded client for automated benchmarking with metrics (throughput, response time) and workloads (e.g., "get all", "put all", "get popular", "mixed").

## Prerequisites
- Ubuntu/Debian-based system (tested on 22.04 LTS) or compatible Linux distribution.
- PostgreSQL 14+ server.
- C++17 compiler (g++ 11+ or clang++).
- Libraries: `libpqxx-dev` (PostgreSQL C++ client), `cpp-httplib` (header-only HTTP library), `nlohmann/json` (header-only JSON).

## Installation

### On Ubuntu/Debian
```bash
# Update package list
sudo apt update

# Install PostgreSQL server and contrib modules
sudo apt install postgresql postgresql-contrib

# Install the C++ connector library (libpqxx)
sudo apt install libpqxx-dev
```

For other distributions, use your package manager (e.g., `yum` on CentOS/RHEL) to install equivalent packages.

## Database Setup
You need to create a PostgreSQL database and a simple key-value table for storage.

1. Log in to the default `postgres` user:
   ```bash
   sudo -u postgres psql
   ```

2. In the `psql` prompt, create a new database:
   ```sql
   CREATE DATABASE kv_system;
   ```

3. Connect to your new database:
   ```sql
   \c kv_system
   ```

4. Create the table to store key-value pairs:
   ```sql
   CREATE TABLE kv_store (
       key   TEXT PRIMARY KEY,
       value TEXT NOT NULL
   );
   ```

5. Create a dedicated user and grant privileges:
   ```sql
   CREATE USER kv_user WITH PASSWORD 'password';
   GRANT ALL PRIVILEGES ON TABLE kv_store TO kv_user;
   ```

6. Exit `psql`:
   ```sql
   \q
   ```

**Note:** Update your `server.cpp` configuration with the database connection details (e.g., host, port, user, password, database name).

```c++
const std::string DB_CONNECTION_STRING = "dbname=kv_system user=kv_user password=password host=localhost sslmode=require";
```

## Building the Server
Ensure `libpqxx-dev` is installed, then compile the server:

```bash
g++ server.cpp -o server -std=c++17 -I. -lpqxx -lpq -pthread -O2
```

This produces an executable named `server`. Repeat similar steps for `load_gen.cpp` if building the client load generator.

## Environment Setup
The setup assumes two machines for isolated testing: one for the server (Machine A) and one for the client load generator (Machine B). If running on a single machine, use `taskset` to pin processes to different CPU cores to avoid resource contention.

### Machine A: Server (PostgreSQL + `./server`)
- Start PostgreSQL as a service:
  ```bash
  sudo systemctl start postgresql
  sudo systemctl enable postgresql  # For auto-start on boot
  ```
- Run the server (listens on port 8080 by default):
  ```bash
  # Pin to cores 0-3 (adjust based on your CPU)
  taskset -c 0-3 ./server
  ```

### Machine B: Client (Load Generator `./load_gen`)
- Run the load generator to simulate traffic:
  ```bash
  # Pin to cores 4-7 (adjust based on your CPU)
  # Syntax: ./load_gen <server_ip> <port> <threads> <duration_seconds> <workload_type>
  taskset -c 4-7 ./load_gen 192.168.1.100 8080 16 300 mixed
  ```
  - `<server_ip>`: IP address of Machine A.
  - `<port>`: Server port (default: 8080).
  - `<threads>`: Number of worker threads.
  - `<duration_seconds>`: Test duration.
  - `<workload>`: Type (e.g., `read`, `write`, `mixed`).

### Single-Machine Setup
If using one machine:
- Ensure sufficient CPU cores (e.g., 8+).
- Pin server to lower cores (0-3) and client to higher cores (4-7) as shown above.

## Usage

### Server Operations
The server exposes HTTP endpoints:
- **POST /kv/<key>**: Store a value (body: JSON `{ "value": "your_data" }`).
- **GET /kv/<key>**: Retrieve a value.
- **DELETE /kv/<key>**: Remove a key-value pair.

Example with `curl`:
```bash
# Store a value
curl -X POST http://localhost:8080/kv -H "Content-Type: application/json" -d '{"key" : "my_key" , "value": "hello world"}'

# Retrieve
curl http://localhost:8080/kv/mykey

# Delete
curl -X DELETE http://localhost:8080/kv/mykey
```

### Load Testing
Use `./load_gen` to benchmark throughput and latency under load. Monitor server logs and PostgreSQL metrics for performance insights.

## Configuration
Edit `server.cpp` for custom settings:
- Database connection string.
- Thread pool size.
- Port and bind address.

## Troubleshooting
- **Compilation errors**: Verify `libpqxx-dev` and PostgreSQL headers are installed.
- **Connection issues**: Check PostgreSQL logs (`sudo journalctl -u postgresql`) and firewall rules (e.g., `ufw allow 8080`).
- **Performance**: Use tools like `pg_top` for DB monitoring or `htop` for CPU pinning verification.

## Contributing
Fork the repo, make changes, and submit a PR. Ensure tests pass and add documentation for new features.