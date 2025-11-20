/*
 * SSTF IO Scheduler
 * Linux Kernel v4.13.9
 * 
 * baseado em noop-iosched.c, por Jens Axboe.
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/div64.h>


static uint64_t total_seek_distance = 0; // Tracks total head movement for the report
static int  queue_size = 64;
static int  max_wait_time = 50;
static bool DEBUG = 0;

module_param(queue_size, int, 0644);
module_param(max_wait_time, int, 0644);
module_param(DEBUG, bool, 0644);

MODULE_PARM_DESC(max_wait_time, "max wait time to request be served in milisseconds.");
MODULE_PARM_DESC(queue_size, "amount of requests.");
MODULE_PARM_DESC(queue_size, "enable printing debug.");


/* estruturas de dados para o SSTF. */
struct sstf_data_s {
	struct list_head queue;
	unsigned long first_req_time;
	int is_batch_active;
};

struct disk_data_s {
	int head_last_pos;	// ultima posição da cabeça
	int head_pos;		// posição atual da cabeça
	char head_dir;		// direçao de acesso ([P]arked, [L]eft, [R]ight)
};

struct disk_data_s disk_data;

/* essa função está igual ao NOOP, realiza merges de blocos adjacentes */
static void sstf_merged_requests(struct request_queue *q, struct request *rq, struct request *next)
{
	list_del_init(&next->queuelist);
}

/* Esta função despacha o próximo bloco a ser lido. */
static int sstf_dispatch(struct request_queue *q, int force)
{
	// if(DEBUG) printk(KERN_ALERT "here1\n");
	struct sstf_data_s *nd = q->elevator->elevator_data;
	struct disk_data_s *disk = &disk_data;
	struct request *rq;
	struct request *best_rq;
	uint64_t time;

	sector_t dist, min_dist = -1ULL;
    sector_t current_pos = disk->head_pos;
    sector_t req_pos;

	int count = 0;
	int dispatched = 0;
	if(DEBUG) printk(KERN_ALERT "here1\n");
	char direction;
	// if(DEBUG) printk(KERN_ALERT "here1\n");
	
	uint64_t now_us = ktime_get_ns();
	if(DEBUG) printk(KERN_ALERT "here2\n");
	now_us = do_div(now_us, 1000);
	if(DEBUG) printk(KERN_ALERT "here3\n");
	uint64_t wait_time_us = (uint64_t) max_wait_time * 1000;

	int is_timeout = 0;
	if(DEBUG) printk(KERN_ALERT "here4\n");
	// if((now_us - nd->first_req_time) > wait_time_us);
	if(DEBUG) printk(KERN_ALERT "here5\n");
	list_for_each_entry(rq, &nd->queue, queuelist){
		count++;
		// if(DEBUG) printk(KERN_ALERT "here6 %d\n", count);
	}

	// no reqs
	// if(DEBUG) printk(KERN_ALERT "here7\n");
	if(count == 0) return 0;
	// if(DEBUG) printk(KERN_ALERT "here8\n");

	// not enough reqs and not timeouting and not being flushed
	if(!force && count < queue_size && !is_timeout){
		// if(DEBUG) printk(KERN_ALERT "here9\n");
		return 0;
	}
	
	/* Aqui deve-se retirar uma requisição da fila e enviá-la para processamento.
	 * Use como exemplo o driver noop-iosched.c. Veja como a requisição é tratada.	 *
	*/
	
	while(!list_empty(&nd->queue)) {
		best_rq = NULL;
		min_dist = -1ULL; //maximum value of the arch
		// if(DEBUG) printk(KERN_ALERT "here10\n");
		
		//selecting smallest dist
		list_for_each_entry(rq, &nd->queue, queuelist){
			req_pos = blk_rq_pos(rq); //sector from request
			
			if(disk->head_pos == -1){ //if position is unknown (after loading)
				best_rq = rq;
				min_dist = 0;
				break;
			}

			//abs distance
			if(req_pos >= current_pos)
				dist = req_pos - current_pos;
			else
				dist = current_pos - req_pos;

			if(dist < min_dist){
				min_dist = dist;
				best_rq = rq;
			}
		}
		//dispatching

		if(best_rq) {
			req_pos = blk_rq_pos(best_rq);

			if (disk->head_pos != -1) {
                sector_t movement = (req_pos >= disk->head_pos) ? 
                                    (req_pos - disk->head_pos) : 
                                    (disk->head_pos - req_pos);
                total_seek_distance += movement;
				disk->head_dir = (req_pos > disk->head_pos) ? 'R' : 'L';
            }

			disk->head_pos = req_pos;
			current_pos = req_pos;

			list_del_init(&best_rq->queuelist);
			elv_dispatch_sort(q, best_rq);
			dispatched++;

			if(DEBUG){
				direction = rq_data_dir(best_rq) == READ ? 'R' : 'W';
				printk(KERN_ALERT "[SSTF] dsp %c %llu (dir: %c)\n", 
                       direction, req_pos, disk->head_dir);
			}

		} else {
			break;
		}

	}

	// rq = list_first_entry_or_null(&nd->queue, struct request, queuelist);

	// if (rq) {
	// 	list_del_init(&rq->queuelist);
	// 	elv_dispatch_sort(q, rq);
		
	// 	direction = rq_data_dir(rq) == READ ? 'R' : 'W';
	// 	time = ktime_get_ns();
	// 	do_div(time, 1000);
	// 	if(DEBUG) printk(KERN_ALERT "[SSTF] dsp %c %llu (%llu us)\n", direction, blk_rq_pos(rq), time);

	// 	return 1;
	// }

	return dispatched;
}



/* Esta função adiciona uma requisição ao disco em uma fila */
static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data_s *nd = q->elevator->elevator_data;
	//struct disk_data_s *disk = &disk_data;
	char direction;
	uint64_t time;
	if(DEBUG) printk(KERN_ALERT "new req\n");

	/* Aqui deve-se adicionar uma requisição na fila do driver.
	 * Use como exemplo o driver noop-iosched.c
	 */
	direction = rq_data_dir(rq) == READ ? 'R' : 'W';
	time = ktime_get_ns();
	do_div(time, 1000);
	if(list_empty(&nd->queue)){
		nd->first_req_time = time;
		nd->is_batch_active = 1;
		if(DEBUG){printk(KERN_ALERT "[SSTF] Batch timer started at: %llu us\n", time);}
	}
	list_add_tail(&rq->queuelist, &nd->queue);
	
	
	if(DEBUG) printk(KERN_ALERT "[SSTF] add %c %llu (%llu us)\n", direction, blk_rq_pos(rq), time);
	
}


/* Esta função inicializa as estruturas de dados necessárias para o escalonador */
static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data_s *nd;
	struct elevator_queue *eq;

	/* Implementação da inicialização da fila (queue).
	 * Use como exemplo a inicialização da fila no driver noop-iosched.c
	 */
	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = nd;

	INIT_LIST_HEAD(&nd->queue);

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	return 0;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data_s *nd = e->elevator_data;

	/* Implementação da finalização da fila (queue).
	 * Use como exemplo o driver noop-iosched.c
	 */
	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

/* Estrutura de dados para os drivers de escalonamento de IO */
static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

/* Inicialização do driver. */
static int sstf_init(void)
{
	int ret;
	struct disk_data_s *disk = &disk_data;
	
	disk->head_last_pos = -1;
	disk->head_pos = -1;
	disk->head_dir = 'P';

	if (queue_size < 20 || queue_size > 100) {
        printk(KERN_WARNING "[SSTF] Warning: queue_size %d is out of bounds (20-100). Defaulting to 64.\n", queue_size);
        queue_size = 64;
    }
	if (max_wait_time < 20 || max_wait_time > 100) {
        printk(KERN_WARNING "[SSTF] Warning: max_wait_time %d is out of bounds (20-100). Defaulting to 50.\n", max_wait_time);
        max_wait_time = 50;
    }

	ret = elv_register(&elevator_sstf);

	if (ret) {
        printk(KERN_ERR "[SSTF] Failed to register scheduler\n");
        return ret;
    }
	
	if(DEBUG) {printk(KERN_ALERT "SSTF driver init\n");}
	
	return 0;
}

/* Finalização do driver. */
static void sstf_exit(void)
{
	printk(KERN_ALERT "SSTF driver exit\n");
	
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_AUTHOR("Sérgio Johann Filho");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler skeleton");
