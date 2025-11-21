/*
 * SSTF IO Scheduler
 * Linux Kernel v4.13.9
 * Based on noop-iosched.c
 */

 #include <linux/blkdev.h>
 #include <linux/elevator.h>
 #include <linux/bio.h>
 #include <linux/module.h>
 #include <linux/slab.h>
 #include <linux/init.h>
 #include <asm/div64.h>
 
 /* Global variables for module parameters */
 static int queue_size = 64;       // Default value
 static int max_wait_time = 50;    // Default value in ms
 static int debug_mode = 1;        // Default 1 (ON) for your testing
 static uint64_t total_seek_distance = 0; // Tracks total head movement for the report
 
 /* Registering the parameters */
 module_param(queue_size, int, 0644);
 MODULE_PARM_DESC(queue_size, "Max requests to enqueue before dispatching (20-100)");
 
 module_param(max_wait_time, int, 0644);
 MODULE_PARM_DESC(max_wait_time, "Max wait time in ms (20-100)");
 
 module_param(debug_mode, int, 0644);
 MODULE_PARM_DESC(debug_mode, "Enable debug logging (1=On, 0=Off)");
 
 
 /* SSTF Data Structures */
 struct sstf_data_s {
	 struct list_head queue;
	 uint64_t first_req_time;   // Nanosecond timestamp for batch start
	 struct timer_list flush_timer; 
     struct request_queue *q;
 };
 
 struct disk_data_s {
	 int head_last_pos;    // Last head position
	 int head_pos;         // Current head position
	 char head_dir;        // Access direction ([P]arked, [L]eft, [R]ight)
 };
 
 struct disk_data_s disk_data;
 
 /* Merge function (same as NOOP) */
 static void sstf_merged_requests(struct request_queue *q, struct request *rq, struct request *next)
 {
	 list_del_init(&next->queuelist);
 }
 
 /* Dispatch Function - Implements the FLUSH LOOP */
 static int sstf_dispatch(struct request_queue *q, int force)
 {
	 struct sstf_data_s *nd = q->elevator->elevator_data;
	 struct disk_data_s *disk = &disk_data;
	 struct request *rq;
	 struct request *best_rq;
	 
	 sector_t dist, min_dist;
	 sector_t current_pos = disk->head_pos;
	 sector_t req_pos;
	 char direction;
	 
	 uint64_t now_ns;
	 uint64_t wait_time_ns; 
	 int count = 0;
	 int dispatched = 0;
	 int is_timeout = 0;
 
	 /* --- PART 1: GATEKEEPER --- */
	 
	 // 1. Count requests
	 list_for_each_entry(rq, &nd->queue, queuelist) {
		 count++;
	 }
 
	 if (count == 0) return 0;
 
	 // 2. Check Time & Thresholds
	 now_ns = ktime_get_ns();
	 wait_time_ns = (uint64_t)max_wait_time * 1000000; 
 
	 // Check timeout logic
	 if ((now_ns - nd->first_req_time) > wait_time_ns) {
		 is_timeout = 1;
	 }
 
	 // DECISION:
	 // If NOT forced, AND queue is small, AND time is NOT up -> Return 0 (Wait)
	 if (!force && count < queue_size && !is_timeout) {
		 return 0; 
	 }
 
	 // If we reach here, the gate is OPEN.
	 // We will now FLUSH the entire queue in a loop.
	//  if (debug_mode) {
	// 	 printk(KERN_ALERT "[SSTF] Gate Open (Count: %d, Timeout: %d). Flushing...\n", count, is_timeout);
	//  }
	 
	 // didnt time out
	 del_timer(&nd->flush_timer);
	 /* --- PART 2: FLUSH LOOP --- */
	 while (!list_empty(&nd->queue)) {
		 best_rq = NULL;
		 min_dist = -1ULL; // Max unsigned long long
 
		 /* A. SELECTOR: Find closest request to CURRENT head position */
		 list_for_each_entry(rq, &nd->queue, queuelist) {
			 req_pos = blk_rq_pos(rq);
			 
			 // Handle Cold Start (Unknown head pos)
			 if (disk->head_pos == -1) {
				 best_rq = rq;
				 min_dist = 0; 
				 break; // Just pick the first one to initialize the head
			 }
 
			 // Calculate Distance
			 if (req_pos >= current_pos)
				 dist = req_pos - current_pos;
			 else
				 dist = current_pos - req_pos;
 
			 // Update Winner
			 if (dist < min_dist) {
				 min_dist = dist;
				 best_rq = rq;
			 }
		 }
 
		 /* B. DISPATCHER: Send winner to block layer */
		 if (best_rq) {
			 req_pos = blk_rq_pos(best_rq);
 
			 // Update Statistics
			 if (disk->head_pos != -1) {
				 sector_t movement = (req_pos >= disk->head_pos) ? 
									 (req_pos - disk->head_pos) : 
									 (disk->head_pos - req_pos);
				 total_seek_distance += movement;
				 disk->head_dir = (req_pos > disk->head_pos) ? 'R' : 'L';
			 }
 
			 // Update Head Position for next iteration
			 disk->head_pos = req_pos;
			 current_pos = req_pos; 
 
			 // Dispatch
			 list_del_init(&best_rq->queuelist);
			 elv_dispatch_sort(q, best_rq);
			 dispatched++;
			 
			 // LOG EVERY DISPATCH
			 if (debug_mode) {
				char rw = (rq_data_dir(best_rq) == READ ? 'R' : 'W');
				u64 now = ktime_get_ns();
			
				// Log Format: event, timestamp, block, rw, direction, current_seek_total
				printk(KERN_DEBUG "[SSTF_STAT] event=DSP ts_ns=%llu block=%llu rw=%c dir=%c seek_total=%llu\n", 
					   now, 
					   (unsigned long long)blk_rq_pos(best_rq), 
					   rw, 
					   disk->head_dir, 
					   total_seek_distance);
			}
		 } else {
			 // Should not happen if list is not empty, but safety break
			 break;
		 }
	 }
 
	 return dispatched;
 }
 
 /* Add Request Function */
 static void sstf_add_request(struct request_queue *q, struct request *rq)
 {
	 struct sstf_data_s *nd = q->elevator->elevator_data;
	 char direction;
	 uint64_t now_ns = ktime_get_ns();
	 uint64_t time_ms; 
 
	 /* Batch Timer Logic */
	 if (list_empty(&nd->queue)) {
		 nd->first_req_time = now_ns;
		 mod_timer(&nd->flush_timer, jiffies + msecs_to_jiffies(max_wait_time));
		//  if (debug_mode) { 
		// 	 printk(KERN_ALERT "[SSTF] New Batch Started (Empty Queue)\n"); 
		//  }

	 }
 
	 list_add_tail(&rq->queuelist, &nd->queue);
	 
	 if (debug_mode) {
		char rw = (rq_data_dir(rq) == READ ? 'R' : 'W');
		u64 now = ktime_get_ns();
		
		// Log Format: event, timestamp, block number, read/write
		printk(KERN_DEBUG "[SSTF_STAT] event=ADD ts_ns=%llu block=%llu rw=%c\n", 
			   now, (unsigned long long)blk_rq_pos(rq), rw);
	}
 }

 static void sstf_timer_expired(unsigned long data)
{
    struct sstf_data_s *nd = (struct sstf_data_s *)data;

    if (debug_mode) {
		u64 now = ktime_get_ns();
		printk(KERN_DEBUG "[SSTF_STAT] event=TIMEOUT ts_ns=%llu\n", now);
	}

    /* "Kick" the block layer to run sstf_dispatch immediately */
    blk_run_queue(nd->q);
}
 
 /* Initialize Queue */
 static int sstf_init_queue(struct request_queue *q, struct elevator_type *e)
 {
	 struct sstf_data_s *nd;
	 struct elevator_queue *eq;
 
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

	 nd->q = q;

	 setup_timer(&nd->flush_timer, sstf_timer_expired, (unsigned long)nd);
 
	 spin_lock_irq(q->queue_lock);
	 q->elevator = eq;
	 spin_unlock_irq(q->queue_lock);
 
	 return 0;
 }
 
 static void sstf_exit_queue(struct elevator_queue *e)
 {
	 struct sstf_data_s *nd = e->elevator_data;
	 //killing the timmer 
	 del_timer_sync(&nd->flush_timer);
 
	 BUG_ON(!list_empty(&nd->queue));
	 kfree(nd);
 }
 
 /* Elevator Structure */
 static struct elevator_type elevator_sstf = {
	 .ops.sq = {
		 .elevator_merge_req_fn      = sstf_merged_requests,
		 .elevator_dispatch_fn       = sstf_dispatch,
		 .elevator_add_req_fn        = sstf_add_request,
		 .elevator_init_fn           = sstf_init_queue,
		 .elevator_exit_fn           = sstf_exit_queue,
	 },
	 .elevator_name = "sstf",
	 .elevator_owner = THIS_MODULE,
 };
 
 /* Module Init */
 static int sstf_init(void)
 {
	 struct disk_data_s *disk = &disk_data;
	 int ret;
 
	 /* Parameter Validation */
	 if (queue_size < 1 || queue_size > 100) {
		 printk(KERN_WARNING "[SSTF] Warning: queue_size %d is out of bounds (1-100). Defaulting to 64.\n", queue_size);
		 queue_size = 64;
	 }
 
	 if (max_wait_time < 20 || max_wait_time > 100) {
		 printk(KERN_WARNING "[SSTF] Warning: max_wait_time %d is out of bounds (20-100). Defaulting to 50.\n", max_wait_time);
		 max_wait_time = 50;
	 }
	 
	 printk(KERN_ALERT "[SSTF] Module Loading... Queue: %d, Max Wait: %d ms\n", queue_size, max_wait_time);
 
	 disk->head_last_pos = -1;
	 disk->head_pos = -1;
	 disk->head_dir = 'P';
	 
	 ret = elv_register(&elevator_sstf);
	 if (ret) {
		 printk(KERN_ERR "[SSTF] Failed to register scheduler\n");
		 return ret;
	 }
	 
	 printk(KERN_ALERT "[SSTF] Driver successfully registered.\n");
 
	 return 0;
 }
 
 /* Module Exit */
 static void sstf_exit(void)
 {
	 printk(KERN_ALERT "[SSTF] driver exit\n");
	 printk(KERN_ALERT "[SSTF] Total Seek Distance: %llu sectors\n", total_seek_distance);
	 elv_unregister(&elevator_sstf);
 }
 
 module_init(sstf_init);
 module_exit(sstf_exit);
 
 MODULE_AUTHOR("Sérgio Johann Filho");
 MODULE_LICENSE("GPL");
 MODULE_DESCRIPTION("SSTF IO scheduler");