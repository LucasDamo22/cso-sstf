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
    sleep 2
}

echo "STARTING AUTOMATED SSTF PERFORMANCE SUITE"
echo "Date: $(date)"
echo ""

# ==============================================================================
# TEST CASE 1: High Load / Happy Path
# ==============================================================================
run_test 1 "High Load (Happy Path)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 200 -p 20 -w 20 -m 1 -M 512"

# ==============================================================================
# TEST CASE 2: Timeout Logic
# ==============================================================================
run_test 2 "Timeout Check (Low Contention)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 10 -p 5 -w 20"

# ==============================================================================
# TEST CASE 3: Write-Intensive
# ==============================================================================
run_test 3 "Write Intensive (80% Writes)" \
    "queue_size=64 max_wait_time=50" \
    "-b 512 -s 8192 -n 100 -p 15 -w 80"

# ==============================================================================
# TEST CASE 4: Small Queue (Granularity Stress)
# ==============================================================================
run_test 4 "Small Queue Constraint" \
    "queue_size=5 max_wait_time=50" \
    "-b 512 -s 8192 -n 200 -p 20 -w 30"

# ==============================================================================
# TEST CASE 5: Fragmentation / Variable Sizes
# ==============================================================================
run_test 5 "Variable Request Sizes" \
    "queue_size=40 max_wait_time=50" \
    "-b 512 -s 8192 -n 150 -p 15 -m 1 -M 512"

echo "ALL TESTS COMPLETED."