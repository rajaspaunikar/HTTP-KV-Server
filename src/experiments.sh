#!/bin/bash

# Configuration
DURATION=300
RESULTS_DIR="results"
mkdir -p $RESULTS_DIR

# Compile code 
# Added -O3 for maximum optimization
g++ server.cpp -o server -std=c++17 -I. -lpqxx -lpq -pthread -O3
g++ -std=c++17 load_generator.cpp -o load_gen -lpthread -O3

# Function to run a single test
run_test() {
    THREADS=$1
    WORKLOAD=$2
    NAME=$3
    
    echo "=================================================="
    echo "STARTING TEST: $NAME | Threads: $THREADS | Duration: ${DURATION}s"
    echo "=================================================="

    # 1. Pin Postgres to Core 0 (Suppress output)
    sudo pgrep -u postgres | xargs -n 1 sudo taskset -cp 0 > /dev/null 2>&1

    # 2. Start Server (Pinned to Core 1) in BACKGROUND
    # CHANGE IS HERE: Redirect to /dev/null to delete logs instantly
    taskset -c 1 ./server > /dev/null 2>&1 &
    
    SERVER_PID=$!
    
    # Wait for server to be ready
    sleep 2

    # 3. Start Load Generator (Pinned to Cores 2-7)
    # Using 6 cores for the generator to ensure it's not the bottleneck
    OUTPUT_FILE="$RESULTS_DIR/${NAME}_t${THREADS}.txt"
    taskset -c 2-7 ./load_gen $THREADS $DURATION $WORKLOAD > $OUTPUT_FILE

    # 4. Kill Server
    kill -9 $SERVER_PID
    
    # 5. Cleanup
    sleep 5
    echo "Test finished. Results saved to $OUTPUT_FILE"
    echo ""
}

# --- EXPERIMENT 1: CPU BOUND (Get Popular) ---
# Workload ID = 3
echo ">>> STARTING CPU BOUND EXPERIMENTS (GET POPULAR) <<<"
for t in 1 2 3 4 5 6 7 8 16 24 32
do
   run_test $t 3 "cpu_bound"
done

# --- EXPERIMENT 2: DISK BOUND (Put All) ---
# Workload ID = 1
echo ">>> STARTING DISK BOUND EXPERIMENTS (PUT ALL) <<<"
for t in 1 2 3 4 5 6 7 8 16 24 32
do
   run_test $t 1 "disk_bound"
done

echo "ALL EXPERIMENTS COMPLETE."