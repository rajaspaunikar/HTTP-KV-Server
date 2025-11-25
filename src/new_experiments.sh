#!/bin/bash

# Configuration
DURATION=300
RESULTS_DIR="results"
mkdir -p $RESULTS_DIR

# --- CONFIGURATION ---
DISK_DEVICE="sda"
WRITE_COL=8
# ---------------------

# Compile code 
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

    # 1. Pin Postgres to Core 0
    sudo pgrep -u postgres | xargs -n 1 sudo taskset -cp 0 > /dev/null 2>&1
    
    # 2. Start Server (Pinned to Core 1)
    taskset -c 1 ./server > /dev/null 2>&1 &
    SERVER_PID=$!
    sleep 2

    # --- MEASUREMENT START ---
    
    # A. CORE 1 CPU MEASUREMENT
    # mpstat -P 1: Monitor ONLY Core 1
    # $DURATION 1: Measure average over the exact duration
    # We save to a temp file to parse later
    mpstat -P 1 $DURATION 1 > cpu_stats.tmp &
    MPSTAT_PID=$!

    # B. DISK MEASUREMENT
    iostat -d -x $DISK_DEVICE $DURATION 2 > disk_stats.tmp &
    IOSTAT_PID=$!
    
    # -----------------------------

    # 3. Start Load Generator (Blocking)
    OUTPUT_FILE="$RESULTS_DIR/${NAME}_t${THREADS}.txt"
    taskset -c 2-7 ./load_gen $THREADS $DURATION $WORKLOAD > $OUTPUT_FILE

    # --- MEASUREMENT END ---
    
    # Wait for measurements to finish
    wait $MPSTAT_PID
    wait $IOSTAT_PID
    
    # A. Parse CPU Stats for Core 1
    # We grep for "Average" and then the column for core "1"
    cpu_line=$(grep "Average" cpu_stats.tmp | grep " 1 " | tail -n 1)
    # The last column ($NF) is %idle. Usage = 100 - %idle
    cpu_usage=$(echo $cpu_line | awk '{print 100 - $NF}')

    # B. Parse Disk Stats
    disk_line=$(grep "^$DISK_DEVICE" disk_stats.tmp | tail -n 1)
    writes_sec=$(echo $disk_line | awk -v col=$WRITE_COL '{print $col}')
    disk_util=$(echo $disk_line | awk '{print $NF}')

    # Handle empty values
    if [ -z "$cpu_usage" ]; then cpu_usage=0; fi
    if [ -z "$writes_sec" ]; then writes_sec=0; fi
    if [ -z "$disk_util" ]; then disk_util=0; fi

    echo "Server CPU Utilization: $cpu_usage" >> $OUTPUT_FILE
    echo "Disk Writes/Sec: $writes_sec" >> $OUTPUT_FILE
    echo "Disk Utilization: $disk_util" >> $OUTPUT_FILE
    
    echo "  -> Core 1 CPU: $cpu_usage%, Writes: $writes_sec/s, Disk Util: $disk_util%"
    
    rm disk_stats.tmp cpu_stats.tmp
    # ---------------------------

    # 4. Kill Server
    kill -9 $SERVER_PID
    
    # 5. Cleanup
    sleep 5
    echo ""
}

# --- EXPERIMENT 1: CPU BOUND (Get Popular) ---
# echo ">>> STARTING CPU BOUND EXPERIMENTS (GET POPULAR) <<<"
# for t in 16
# do
#    run_test $t 3 "cpu_bound"
# done

# --- EXPERIMENT 2: DISK BOUND (Put All) ---
echo ">>> STARTING DISK BOUND EXPERIMENTS (PUT ALL) <<<"
for t in 1 2 3 4 5 6 7 8 12 16 24 32
do
   run_test $t 1 "disk_bound"
done

echo "ALL EXPERIMENTS COMPLETE."