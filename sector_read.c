/*
 * Simple disk I/O generator
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define SECTOR_SIZE	512
#define DISK_SZ		(4096 * 1024)
#define N_ACCESSES	50

int main()
{
	int ret, fd, pid, i;
	unsigned int pos;
	char buf[SECTOR_SIZE];

	printf("Starting sector read example...\n");

	/* limpa buffers e caches de disco */
	printf("Cleaning disk cache...\n");
	system("echo 3 > /proc/sys/vm/drop_caches"); 

	/* a) desabilita merges de i/o
	 * b) define 4Kb para o tamanho máximo de uma requisição 
	 * c) evita que o sistema faça a leitura de mais conteúdo do que o que foi requisitado
	 */
	printf("Configuring scheduling queues...\n");
	system("echo 2 > /sys/block/sdb/queue/nomerges");
	system("echo 4 > /sys/block/sdb/queue/max_sectors_kb");
	system("echo 0 > /sys/block/sdb/queue/read_ahead_kb");

	srand(getpid());

	fd = open("/dev/sdb", O_RDWR);
	
	if (fd < 0){
		perror("Failed to open the device...");
		return errno;
	}

	strcpy(buf, "hello world!");

	for (i = 0; i < N_ACCESSES; i++) {
		pos = (rand() % (DISK_SZ / SECTOR_SIZE));
		printf("pid %d, block %d\n", getpid(), pos);
		
		/* Set position */
		lseek(fd, pos * SECTOR_SIZE, SEEK_SET);
		/* Peform read. */
		read(fd, buf, 500);
	}
	close(fd);

	return 0;
}

