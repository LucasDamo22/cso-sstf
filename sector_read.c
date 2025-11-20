/* sector_read_fixed.c */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h> // Required for wait()

#define SECTOR_SIZE	512
#define DISK_SZ		(4096 * 1024) // Adjust based on your virtual disk size
#define N_ACCESSES	10
#define N_PROCESSES 10 // Create enough processes to fill the queue

void run_workload() {
    int fd, i;
    unsigned int pos;
    char buf[SECTOR_SIZE];

    srand(getpid()); // Seed random with unique PID

    fd = open("/dev/sdb", O_RDWR); // Ensure /dev/sdb is correct
    if (fd < 0){
        perror("Failed to open the device");
        exit(1);
    }

    for (i = 0; i < N_ACCESSES; i++) {
        // Generate random block position
        pos = (rand() % (DISK_SZ / SECTOR_SIZE));
        
        // lseek and read are synchronous (blocking)
        lseek(fd, pos * SECTOR_SIZE, SEEK_SET);
        read(fd, buf, 500); 
    }
    close(fd);
    exit(0);
}

int main()
{
    int i;

    printf("Starting concurrent sector read example...\n");

    // Standard cleanup
    system("echo 3 > /proc/sys/vm/drop_caches"); 
    system("echo 2 > /sys/block/sdb/queue/nomerges");
    system("echo 4 > /sys/block/sdb/queue/max_sectors_kb");
    system("echo 0 > /sys/block/sdb/queue/read_ahead_kb");

    // Create concurrent processes
    for(i = 0; i < N_PROCESSES; i++) {
        if(fork() == 0) {
            // Child process
            run_workload();
        }
    }

    // Parent waits for all children to finish
    while(wait(NULL) > 0);

    printf("All processes finished.\n");
    return 0;
}