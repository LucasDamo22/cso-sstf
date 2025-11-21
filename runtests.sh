#!/bin/sh

# Function to run a single test case
# Usage: run_test <Case Number> <Description> <Module Params> <App Params>
run_test() {
    CASE_NUM=$1
    DESC=$2
    MOD_PARAMS=$3
    APP_PARAMS=$4

    echo "==========================================================="
    echo "TEST CASE $CASE_NUM: $DESC"
    echo "-----------------------------------------------------------"
    echo "1. Cleaning environment..."
    # Ensure clean state
    echo noop > /sys/block/sdb/queue/scheduler 2>/dev/null
    rmmod sstf-iosched 2>/dev/null
    
    dmesg -c > /dev/null                 # Clear kernel buffer
    echo "3" > /proc/sys/vm/drop_caches  # Clear disk cache
    echo "4 4 1 7" > /proc/sys/kernel/printk # Silence console logs

    echo "2. Loading Module: modprobe sstf-iosched $MOD_PARAMS"
    if ! modprobe sstf-iosched $MOD_PARAMS; then
        echo "CRITICAL ERROR: Failed to load module with modprobe."
        return 1
    fi

    echo "3. Activating Scheduler: echo sstf > /sys/block/sdb/queue/scheduler"
    if ! echo sstf > /sys/block/sdb/queue/scheduler; then
        echo "CRITICAL ERROR: Failed to select sstf scheduler."
        rmmod sstf-iosched
        return 1
    fi

    echo "4. Running Workload: /bin/sector_read $APP_PARAMS"
    /bin/sector_read $APP_PARAMS > /dev/null 2>&1

    echo "5. Restoring Scheduler: echo noop > /sys/block/sdb/queue/scheduler"
    echo noop > /sys/block/sdb/queue/scheduler

    echo "6. Unloading module to generate report..."
    rmmod sstf-iosched

    echo "-----------------------------------------------------------"
    echo "RESULT REPORT (From Kernel Log):"
    # Extract only the lines starting with [SSTF] from the dmesg buffer
    dmesg | grep "\[SSTF\]"
    echo "==========================================================="
    echo ""
    # Small pause to let disk settle
    sleep 2
}

echo "STARTING AUTOMATED SSTF PERFORMANCE SUITE"
echo "Date: $(date)"
echo ""

# ==============================================================================
# TEST CASE 1: High Load / Happy Path
# WHY: Tests the algorithm's maximum efficiency. With 200 requests in a queue of 64,
# the scheduler has many options to pick the 'closest' sector.
# EXPECTATION: Highest Economy of Movement (Seek Reduction).
# ==============================================================================
run_test 1 "High Load (Happy Path)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 200 -p 20 -w 20 -m 1 -M 512"

# ==============================================================================
# TEST CASE 2: Timeout Logic
# WHY: Queue size is 64, but we only send 50 requests total. The queue never fills.
# The ONLY way this finishes is if your timer correctly fires after 50ms.
# EXPECTATION: Test completes successfully (does not hang).
# ==============================================================================
run_test 2 "Timeout Check (Low Contention)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 10 -p 5 -w 20"

# ==============================================================================
# TEST CASE 3: Write-Intensive
# WHY: Simulates a workload with 80% writes. Disk schedulers often handle writes
# differently (merges). SSTF should still provide seek reduction.
# EXPECTATION: High reduction, proves stability under write pressure.
# ==============================================================================
run_test 3 "Write Intensive (80% Writes)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 100 -p 15 -w 80"

# ==============================================================================
# TEST CASE 4: Small Queue (Granularity Stress)
# WHY: Forces the batch size to be very small (5). The scheduler flushes often.
# With fewer requests to sort, the optimization is naturally worse than Case 1.
# EXPECTATION: Lower Economy of Movement compared to Case 1.
# ==============================================================================
run_test 4 "Small Queue Constraint" \
    "queue_size=5 max_wait_time=50" \
    "-b 512 -s 8192 -n 200 -p 20 -w 30"

# ==============================================================================
# TEST CASE 5: Fragmentation / Variable Sizes
# WHY: Sends requests of random sizes (1 byte to 512 bytes). This tests if your
# logic correctly handles sector alignment and robustness.
# EXPECTATION: Moderate reduction, no kernel crashes.
# ==============================================================================
run_test 5 "Variable Request Sizes" \
    "queue_size=40 max_wait_time=50" \
    "-b 512 -s 8192 -n 150 -p 15 -m 1 -M 512"

echo "ALL TESTS COMPLETED."