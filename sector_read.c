/*
 * sstf_test_app.c
 * Aplicação de teste parametrizável para Scheduler SSTF
 * Versão Simplificada (Sem O_DIRECT)
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/wait.h>
 #include <time.h>
 #include <string.h>
 #include <getopt.h>
 
 // Valores padrão
 #define DFL_BLOCK_SIZE 512
 #define DFL_DISK_BLOCKS 8192
 #define DFL_NUM_OPS 100
 #define DFL_WRITE_PCT 30
 #define DFL_PROCS 10
 #define DFL_DEVICE "/dev/sdb"
 
 // Variáveis Globais de Configuração
 int block_size = DFL_BLOCK_SIZE;
 int disk_blocks = DFL_DISK_BLOCKS;
 int num_ops = DFL_NUM_OPS;
 int write_pct = DFL_WRITE_PCT;
 int min_req_size = 1;
 int max_req_size = DFL_BLOCK_SIZE;
 int num_procs = DFL_PROCS;
 char device_path[64] = DFL_DEVICE;
 
 void print_usage(char *prog) {
     printf("Uso: %s [opcoes]\n", prog);
     printf("  -d <path>   Dispositivo (padrao: /dev/sdb)\n");
     printf("  -b <bytes>  Tamanho do bloco (ex: 512)\n");
     printf("  -s <blocos> Tamanho do disco em blocos (ex: 8192)\n");
     printf("  -n <ops>    Numero de operacoes I/O por processo\n");
     printf("  -w <pct>    Percentual de escritas (0-100)\n");
     printf("  -p <procs>  Numero de processos concorrentes\n");
     printf("  -m <bytes>  Tamanho minimo da requisicao\n");
     printf("  -M <bytes>  Tamanho maximo da requisicao\n");
     exit(1);
 }
 
 void run_worker(int id) {
     int fd, i;
     unsigned int pos;
     int req_size;
     char *buf;
     int is_write;
     
     // Seed único para garantir aleatoriedade real entre processos
     srand(time(NULL) ^ (getpid() << 16));
 
     // Alocação de memória padrão (simples malloc)
     buf = malloc(max_req_size);
     if (!buf) {
         perror("Erro no malloc");
         exit(1);
     }
     
     // Preenche com algum dado para escritas
     memset(buf, 'A', max_req_size);
 
     // Abertura padrão do dispositivo (com cache do SO)
     fd = open(device_path, O_RDWR);
     if (fd < 0) {
         perror("Erro ao abrir disco");
         free(buf);
         exit(1);
     }
 
     for (i = 0; i < num_ops; i++) {
         // 1. Define posição aleatória (garantindo que cabe no disco)
         pos = rand() % (disk_blocks - (max_req_size / block_size));
         
         // 2. Define tamanho aleatório
         req_size = min_req_size + (rand() % (max_req_size - min_req_size + 1));
 
         // 3. Define se é Leitura ou Escrita
         is_write = ((rand() % 100) < write_pct);
 
         // Posiciona a cabeça de leitura
         lseek(fd, pos * block_size, SEEK_SET);
 
         // Executa a operação
         if (is_write) {
             write(fd, buf, req_size);
         } else {
             read(fd, buf, req_size);
         }
     }
 
     free(buf);
     close(fd);
     exit(0);
 }
 
 int main(int argc, char **argv) {
     int opt, i;
 
     // Processa argumentos da linha de comando
     while ((opt = getopt(argc, argv, "d:b:s:n:w:p:m:M:h")) != -1) {
         switch (opt) {
             case 'd': strncpy(device_path, optarg, 63); break;
             case 'b': block_size = atoi(optarg); break;
             case 's': disk_blocks = atoi(optarg); break;
             case 'n': num_ops = atoi(optarg); break;
             case 'w': write_pct = atoi(optarg); break;
             case 'p': num_procs = atoi(optarg); break;
             case 'm': min_req_size = atoi(optarg); break;
             case 'M': max_req_size = atoi(optarg); break;
             case 'h': print_usage(argv[0]); break;
             default: print_usage(argv[0]); break;
         }
     }
 
     if (min_req_size > block_size || max_req_size > block_size) {
         printf("Erro: Requisicao deve ser menor ou igual ao bloco (%d)\n", block_size);
         return 1;
     }
 
     printf("=== Iniciando Teste SSTF (Simples) ===\n");
     printf("Device: %s | Procs: %d | Ops: %d\n", device_path, num_procs, num_ops);
     
     // Tenta limpar cache antes de começar (ajuda um pouco)
     system("echo 3 > /proc/sys/vm/drop_caches");
 
     // Cria os processos concorrentes
     for (i = 0; i < num_procs; i++) {
         if (fork() == 0) {
             run_worker(i);
         }
     }
 
     // Aguarda todos terminarem
     while(wait(NULL) > 0);
 
     printf("=== Teste Finalizado ===\n");
     return 0;
 }