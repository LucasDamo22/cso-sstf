/* sector_read_param.c */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <getopt.h>

#define SECTOR_SIZE 512
// Default to 4MB disk (8192 sectors)
#define DISK_SZ_SECTORS 8192 

// Global defaults
int n_processes = 20;
int n_accesses = 50;
int is_random = 1;
char device_path[32] = "/dev/sdb";

void run_workload(int p_id) {
    int fd, i;
    unsigned int pos;
    char buf[SECTOR_SIZE];

    // Seed random differently for each process
    srand(time(NULL) ^ (getpid() << 16));

    fd = open(device_path, O_RDWR);
    if (fd < 0){
        perror("Failed to open device");
        exit(1);
    }

    for (i = 0; i < n_accesses; i++) {
        if (is_random) {
            pos = (rand() % DISK_SZ_SECTORS);
        } else {
            // Sequential access (good for testing NOOP)
            pos = (i * 10) % DISK_SZ_SECTORS; 
        }

        lseek(fd, pos * SECTOR_SIZE, SEEK_SET);
        read(fd, buf, 500); 
    }
    close(fd);
    exit(0);
}

void print_usage(char *prog) {
    printf("Usage: %s [-p processes] [-n accesses] [-d device] [-s (sequential)]\n", prog);
}

int main(int argc, char *argv[]) {
    int i, opt;

    // Parse Command Line Arguments
    while ((opt = getopt(argc, argv, "p:n:d:sh")) != -1) {
        switch (opt) {
            case 'p': n_processes = atoi(optarg); break;
            case 'n': n_accesses = atoi(optarg); break;
            case 'd': strcpy(device_path, optarg); break;
            case 's': is_random = 0; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    printf("Starting Test: %d processes, %d accesses each, Device: %s, Mode: %s\n", 
           n_processes, n_accesses, device_path, is_random ? "RANDOM" : "SEQUENTIAL");

    // Configure Kernel Queues (Crucial for correct testing)
    system("echo 3 > /proc/sys/vm/drop_caches");
    
    // Construct the command string dynamically
    char cmd[128];
    sprintf(cmd, "echo 2 > /sys/block/%s/queue/nomerges", device_path+5); // +5 skips "/dev/"
    system(cmd);
    
    sprintf(cmd, "echo 4 > /sys/block/%s/queue/max_sectors_kb", device_path+5);
    system(cmd);
    
    sprintf(cmd, "echo 0 > /sys/block/%s/queue/read_ahead_kb", device_path+5);
    system(cmd);

    // Spawn Processes
    for(i = 0; i < n_processes; i++) {
        if(fork() == 0) {
            run_workload(i);
        }
    }

    // Wait for completion
    while(wait(NULL) > 0);
    
    printf("Test Complete.\n");
    return 0;
}