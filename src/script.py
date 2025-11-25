import os
import re
import matplotlib.pyplot as plt

SOURCE_FOLDER = 'results' 

def parse_file(filename):
    filepath = os.path.join(SOURCE_FOLDER, filename)
    try:
        with open(filepath, 'r') as f:
            content = f.read()
    except: return None

    # Regex for all metrics
    threads_match = re.search(r'Threads:\s+(\d+)', content)
    throughput_match = re.search(r'Average Throughput:\s+([\d\.]+)', content)
    latency_match = re.search(r'Average Response Time:\s+([\d\.]+)', content)
    cpu_match = re.search(r'Server CPU Utilization:\s+([\d\.]+)', content)
    
    # NEW: Regex for Disk
    writes_match = re.search(r'Disk Writes/Sec:\s+([\d\.]+)', content)
    disk_util_match = re.search(r'Disk Utilization:\s+([\d\.]+)', content)

    if threads_match and throughput_match:
        data = {
            'threads': int(threads_match.group(1)),
            'throughput': float(throughput_match.group(1)),
            'latency': float(latency_match.group(1)),
            'cpu': float(cpu_match.group(1)) if cpu_match else 0.0,
            'disk_writes': float(writes_match.group(1)) if writes_match else 0.0,
            'disk_util': float(disk_util_match.group(1)) if disk_util_match else 0.0
        }
        return data
    return None

def get_data(pattern_prefix):
    x, y_thru, y_lat, y_cpu, y_writes, y_util = [], [], [], [], [], []
    
    files = [f for f in os.listdir(SOURCE_FOLDER) if f.startswith(pattern_prefix) and f.endswith(".txt")]
    parsed = [parse_file(f) for f in files if parse_file(f)]
    parsed.sort(key=lambda x: x['threads'])
    
    for d in parsed:
        x.append(d['threads'])
        y_thru.append(d['throughput'])
        y_lat.append(d['latency'])
        y_cpu.append(d['cpu'])
        y_writes.append(d['disk_writes'])
        y_util.append(d['disk_util'])
        
    return x, y_thru, y_lat, y_cpu, y_writes, y_util

def plot_graph(x, y, title, ylabel, filename, color):
    if not x: return
    plt.figure(figsize=(10, 6))
    plt.plot(x, y, marker='o', linewidth=2, color=color)
    plt.title(title, fontsize=14)
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel(ylabel, fontsize=12)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.xticks(x)
    
    # Add labels
    for i, j in zip(x, y):
        plt.annotate(f"{j:.1f}", (i, j), textcoords="offset points", 
                     xytext=(0, 10), ha='center', fontsize=9, fontweight='bold')

    plt.savefig(filename)
    print(f"Saved {filename}")
    plt.close()

# --- Main ---
print("Generating 8 Graphs...")

# Get Data
c_x, c_thru, c_lat, c_cpu, c_w, c_ut = get_data("cpu_bound_t")
d_x, d_thru, d_lat, d_cpu, d_w, d_ut = get_data("disk_bound_t")

# 1. CPU Bound Workload Graphs
plot_graph(c_x, c_thru, "CPU Bound: Throughput", "Req/Sec", "cpu_1_throughput.png", "tab:blue")
plot_graph(c_x, c_lat,  "CPU Bound: Latency", "ms", "cpu_2_latency.png", "tab:red")
plot_graph(c_x, c_cpu,  "CPU Bound: Server CPU Util", "%", "cpu_3_cpu_util.png", "tab:purple")
# This should be ZERO for CPU bound
plot_graph(c_x, c_ut,   "CPU Bound: Disk Util (Proof)", "%", "cpu_4_disk_util.png", "tab:grey")

# 2. Disk Bound Workload Graphs
plot_graph(d_x, d_thru, "Disk Bound: Throughput", "Req/Sec", "disk_1_throughput.png", "tab:green")
plot_graph(d_x, d_lat,  "Disk Bound: Latency", "ms", "disk_2_latency.png", "tab:orange")
plot_graph(d_x, d_cpu,  "Disk Bound: Server CPU Util", "%", "disk_3_cpu_util.png", "tab:brown")
# This is your PROOF of bottleneck
plot_graph(d_x, d_ut,    "Disk Bound: Disk Util (Proof)", "%", "disk_4_disk_util.png", "black")
plot_graph(d_x, d_w,    "Disk Bound: Disk Writes/Sec (Proof)", "%", "disk_5_disk_writes.png", "black")

print("Done.")