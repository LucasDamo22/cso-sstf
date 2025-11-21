/*
 * SSTF IO Scheduler - Com Comparação FCFS vs SSTF
 * Linux Kernel v4.13.9
 */
 #include <linux/blkdev.h>
 #include <linux/elevator.h>
 #include <linux/bio.h>
 #include <linux/module.h>
 #include <linux/slab.h>
 #include <linux/init.h>
 #include <asm/div64.h>
 
 /* Parâmetros do Módulo */
 static int queue_size = 64;
 static int max_wait_time = 50;
 static int debug_mode = 1;
 
 /* Estatísticas Globais para o Relatório Final */
 static u64 total_sstf_seek = 0; // Distância REAL percorrida
 static u64 total_fcfs_seek = 0; // Distância TEÓRICA (sem reordenação)
 
 module_param(queue_size, int, 0644);
 MODULE_PARM_DESC(queue_size, "Max requests to enqueue (20-100)");
 module_param(max_wait_time, int, 0644);
 MODULE_PARM_DESC(max_wait_time, "Max wait time in ms (20-100)");
 module_param(debug_mode, int, 0644);
 MODULE_PARM_DESC(debug_mode, "Enable debug logging");
 
 struct sstf_data_s {
	 struct list_head queue;
	 u64 first_req_time;
	 struct timer_list flush_timer; 
	 struct request_queue *q;
 };
 
 /* Estrutura para rastrear a posição da cabeça */
 struct disk_data_s {
	 int head_pos;           // Posição REAL da cabeça (SSTF)
	 int last_arrival_pos;   // Posição VIRTUAL da cabeça (Simulação FCFS)
	 char head_dir;          // Direção atual
 };
 
 struct disk_data_s disk_data;
 
 static void sstf_merged_requests(struct request_queue *q, struct request *rq, struct request *next) {
	 list_del_init(&next->queuelist);
 }
 
 /* Callback do Timer (para Kernel 4.13) */
 static void sstf_timer_expired(unsigned long data) {
	 struct sstf_data_s *nd = (struct sstf_data_s *)data;
	 if (debug_mode) printk(KERN_DEBUG "[SSTF] Timer Expired! Kicking queue.\n");
	 blk_run_queue(nd->q);
 }
 
 /* Função Dispatch: Executa a lógica SSTF e conta a distância REAL */
 static int sstf_dispatch(struct request_queue *q, int force) {
	 struct sstf_data_s *nd = q->elevator->elevator_data;
	 struct disk_data_s *disk = &disk_data;
	 struct request *rq, *best_rq;
	 sector_t dist, min_dist;
	 sector_t current_pos = disk->head_pos;
	 sector_t req_pos;
	 u64 now_ns = ktime_get_ns();
	 u64 wait_time_ns = (u64)max_wait_time * 1000000;
	 int count = 0, dispatched = 0, is_timeout = 0;
 
	 list_for_each_entry(rq, &nd->queue, queuelist) count++;
 
	 if (count == 0) return 0;
	 if ((now_ns - nd->first_req_time) > wait_time_ns) is_timeout = 1;
 
	 // Gatekeeper: Abre se forçado, fila cheia ou timeout
	 if (!force && count < queue_size && !is_timeout) return 0;
 
	 del_timer(&nd->flush_timer);
 
	 // Flush Loop
	 while (!list_empty(&nd->queue)) {
		 best_rq = NULL;
		 min_dist = -1ULL;
 
		 // SSTF: Encontra o mais próximo da cabeça ATUAL
		 list_for_each_entry(rq, &nd->queue, queuelist) {
			 req_pos = blk_rq_pos(rq);
			 
			 if (disk->head_pos == -1) { // Inicialização
				 best_rq = rq; min_dist = 0; break; 
			 }
 
			 if (req_pos >= current_pos) dist = req_pos - current_pos;
			 else dist = current_pos - req_pos;
 
			 if (dist < min_dist) {
				 min_dist = dist;
				 best_rq = rq;
			 }
		 }
 
		 if (best_rq) {
			 req_pos = blk_rq_pos(best_rq);
 
			 // Atualiza métrica SSTF (REAL)
			 if (disk->head_pos != -1) {
				 sector_t movement = (req_pos >= disk->head_pos) ? (req_pos - disk->head_pos) : (disk->head_pos - req_pos);
				 total_sstf_seek += movement;
				 disk->head_dir = (req_pos > disk->head_pos) ? 'R' : 'L';
			 } else {
				 disk->head_pos = req_pos; // Primeiro setup
			 }
 
			 disk->head_pos = req_pos;
			 current_pos = req_pos;
 
			 list_del_init(&best_rq->queuelist);
			 elv_dispatch_sort(q, best_rq);
			 dispatched++;
 
			 // Log: Ordem de Atendimento (SERVICE ORDER)
			 if (debug_mode) {
				 printk(KERN_DEBUG "[SSTF_LOG] SERVED block=%llu dir=%c total_sstf=%llu\n", 
					 (unsigned long long)req_pos, disk->head_dir, total_sstf_seek);
			 }
		 } else {
			 break;
		 }
	 }
	 return dispatched;
 }
 
 /* Função Add Request: Simula a lógica FCFS para comparação */
 static void sstf_add_request(struct request_queue *q, struct request *rq) {
	 struct sstf_data_s *nd = q->elevator->elevator_data;
	 struct disk_data_s *disk = &disk_data;
	 sector_t req_pos = blk_rq_pos(rq);
	 
	 if (list_empty(&nd->queue)) {
		 nd->first_req_time = ktime_get_ns();
		 mod_timer(&nd->flush_timer, jiffies + msecs_to_jiffies(max_wait_time));
	 }
 
	 list_add_tail(&rq->queuelist, &nd->queue);
 
	 /* CÁLCULO DA SIMULAÇÃO FCFS (Sem Reordenação) */
	 // Se last_arrival_pos for -1, assumimos que é o início e não conta seek
	 if (disk->last_arrival_pos != -1) {
		 sector_t movement = (req_pos >= disk->last_arrival_pos) ? 
							 (req_pos - disk->last_arrival_pos) : 
							 (disk->last_arrival_pos - req_pos);
		 total_fcfs_seek += movement;
	 }
	 // Atualiza a "cabeça virtual" para o próximo cálculo
	 disk->last_arrival_pos = req_pos;
 
	 // Log: Ordem de Chegada (ARRIVAL ORDER)
	 if (debug_mode) {
		 char rw = (rq_data_dir(rq) == READ ? 'R' : 'W');
		 printk(KERN_DEBUG "[SSTF_LOG] ARRIVED block=%llu rw=%c sim_fcfs_total=%llu\n", 
				(unsigned long long)req_pos, rw, total_fcfs_seek);
	 }
 }
 
 static int sstf_init_queue(struct request_queue *q, struct elevator_type *e) {
	 struct sstf_data_s *nd;
	 struct elevator_queue *eq;
 
	 eq = elevator_alloc(q, e);
	 if (!eq) return -ENOMEM;
 
	 nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	 if (!nd) { kobject_put(&eq->kobj); return -ENOMEM; }
	 
	 eq->elevator_data = nd;
	 INIT_LIST_HEAD(&nd->queue);
	 
	 nd->q = q;
	 setup_timer(&nd->flush_timer, sstf_timer_expired, (unsigned long)nd);
 
	 spin_lock_irq(q->queue_lock);
	 q->elevator = eq;
	 spin_unlock_irq(q->queue_lock);
	 return 0;
 }
 
 static void sstf_exit_queue(struct elevator_queue *e) {
	 struct sstf_data_s *nd = e->elevator_data;
	 del_timer_sync(&nd->flush_timer);
	 BUG_ON(!list_empty(&nd->queue));
	 kfree(nd);
 }
 
 static struct elevator_type elevator_sstf = {
	 .ops.sq = {
		 .elevator_merge_req_fn = sstf_merged_requests,
		 .elevator_dispatch_fn  = sstf_dispatch,
		 .elevator_add_req_fn   = sstf_add_request,
		 .elevator_init_fn      = sstf_init_queue,
		 .elevator_exit_fn      = sstf_exit_queue,
	 },
	 .elevator_name = "sstf",
	 .elevator_owner = THIS_MODULE,
 };
 
 static int sstf_init(void) {
	 disk_data.head_pos = -1;
	 disk_data.last_arrival_pos = -1; // Inicia cabeça virtual
	 disk_data.head_dir = 'P';
	 
	 printk(KERN_ALERT "[SSTF] Loaded. Queue=%d Wait=%dms\n", queue_size, max_wait_time);
	 return elv_register(&elevator_sstf);
 }
 
 static void sstf_exit(void) {
	 /* Exibe o relatório final solicitado no PDF */
	 printk(KERN_ALERT "===========================================\n");
	 printk(KERN_ALERT "[SSTF] RELATÓRIO FINAL DE DESEMPENHO\n");
	 printk(KERN_ALERT "[SSTF] Distância simulada (FCFS): %llu setores\n", total_fcfs_seek);
	 printk(KERN_ALERT "[SSTF] Distância real (SSTF):     %llu setores\n", total_sstf_seek);
	 
	 if (total_fcfs_seek > 0) {
		 u64 saved = total_fcfs_seek - total_sstf_seek;
		 // Cálculo simples de porcentagem em integer math
		 u64 pct = (saved * 100);
		 do_div(pct, total_fcfs_seek);
		 printk(KERN_ALERT "[SSTF] Economia de Movimento: %llu%%\n", pct);
	 }
	 printk(KERN_ALERT "===========================================\n");
	 elv_unregister(&elevator_sstf);
 }
 
 module_init(sstf_init);
 module_exit(sstf_exit);
 MODULE_LICENSE("GPL");