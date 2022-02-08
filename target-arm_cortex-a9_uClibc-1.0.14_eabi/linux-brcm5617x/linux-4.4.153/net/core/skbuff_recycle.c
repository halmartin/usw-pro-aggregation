/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 *      Generic skb recycler
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include <linux/string.h>

static struct proc_dir_entry *proc_net_skbrecycler;

static __always_inline void zero_struct(void *v, int size)
{
	u32 *s = (u32 *)v;

	/* We assume that size is word aligned; in fact, it's constant */
	WARN_ON((size & 3) != 0);

	/* This looks odd but we "know" size is a constant, and so the
	 * compiler can fold away all of the conditionals.  The compiler is
	 * pretty smart here, and can fold away the loop, too!
	 */
	while (size > 0) {
		if (size >= 4)
			s[0] = 0;
		if (size >= 8)
			s[1] = 0;
		if (size >= 12)
			s[2] = 0;
		if (size >= 16)
			s[3] = 0;
		if (size >= 20)
			s[4] = 0;
		if (size >= 24)
			s[5] = 0;
		if (size >= 28)
			s[6] = 0;
		if (size >= 32)
			s[7] = 0;
		if (size >= 36)
			s[8] = 0;
		if (size >= 40)
			s[9] = 0;
		if (size >= 44)
			s[10] = 0;
		if (size >= 48)
			s[11] = 0;
		if (size >= 52)
			s[12] = 0;
		if (size >= 56)
			s[13] = 0;
		if (size >= 60)
			s[14] = 0;
		if (size >= 64)
			s[15] = 0;
		size -= 64;
		s += 16;
	}
}
#ifdef memset
#undef memset
#define memset(ptr, value, sz) zero_struct(ptr, sz)
#endif

#ifdef CONFIG_SKB_RECYCLER

#define SKB_RECYCLE_SIZE	2304
#define SKB_RECYCLE_MIN_SIZE	SKB_RECYCLE_SIZE
#define SKB_RECYCLE_MAX_SIZE	(3904 - NET_SKB_PAD)
#define SKB_RECYCLE_MAX_SKBS	1024

#define SKB_RECYCLE_SPARE_MAX_SKBS		256

#ifdef CONFIG_SKB_RECYCLER_PREALLOC
#define SKB_RECYCLE_MAX_PREALLOC_SKBS CONFIG_SKB_RECYCLE_MAX_PREALLOC_SKBS
#define SKB_RECYCLE_MAX_SHARED_POOLS \
	DIV_ROUND_UP(SKB_RECYCLE_MAX_PREALLOC_SKBS, \
		     SKB_RECYCLE_SPARE_MAX_SKBS)
#else
#define SKB_RECYCLE_MAX_SHARED_POOLS            8
#endif

#define SKB_RECYCLE_MAX_SHARED_POOLS_MASK	\
	(SKB_RECYCLE_MAX_SHARED_POOLS - 1)

static DEFINE_PER_CPU(struct sk_buff_head, recycle_list);
static int skb_recycle_max_skbs = SKB_RECYCLE_MAX_SKBS;
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
static DEFINE_PER_CPU(struct sk_buff_head, recycle_spare_list);
static int skb_recycle_spare_max_skbs = SKB_RECYCLE_SPARE_MAX_SKBS;
static struct global_recycler glob_recycler;
#endif

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
struct global_recycler {
	/* Global circular list which holds the shared skb pools */
	struct sk_buff_head pool[SKB_RECYCLE_MAX_SHARED_POOLS];
	u8 head;		/* head of the circular list */
	u8 tail;		/* tail of the circular list */
	spinlock_t lock;
};
#endif

static void skb_release_data(struct sk_buff *skb);
static void kfree_skbmem(struct sk_buff *skb);

__attribute__ ((optimize(3))) static inline bool consume_skb_can_recycle(const struct sk_buff *skb,
					   int min_skb_size, int max_skb_size)
{
	if (unlikely(irqs_disabled()))
		return false;

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_DEV_ZEROCOPY))
		return false;

	if (unlikely(skb_is_nonlinear(skb)))
		return false;

	if (unlikely(skb->fclone != SKB_FCLONE_UNAVAILABLE))
		return false;

	min_skb_size = SKB_DATA_ALIGN(min_skb_size + NET_SKB_PAD);
	if (unlikely(skb_end_pointer(skb) - skb->head < min_skb_size))
		return false;

	max_skb_size = SKB_DATA_ALIGN(max_skb_size + NET_SKB_PAD);
	if (unlikely(skb_end_pointer(skb) - skb->head > max_skb_size))
		return false;

	if (unlikely(skb_cloned(skb)))
		return false;

	if (unlikely(skb_pfmemalloc(skb)))
		return false;

	return true;
}
static inline struct sk_buff *skb_recycler_alloc(struct net_device *dev,
					  unsigned int length)
{
	unsigned long flags;
	struct sk_buff_head *h;
	struct sk_buff *skb = NULL;

	if (unlikely(length > SKB_RECYCLE_SIZE))
		return NULL;

	h = &get_cpu_var(recycle_list);
	local_irq_save(flags);
	skb = __skb_dequeue(h);
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	if (unlikely(!skb)) {
		u8 head;

		spin_lock(&glob_recycler.lock);
		/* If global recycle list is not empty, use global buffers */
		head = glob_recycler.head;
		if (unlikely(head == glob_recycler.tail)) {
			spin_unlock(&glob_recycler.lock);
		} else {
			/* Move SKBs from global list to CPU pool */
			skb_queue_splice_init(&glob_recycler.pool[head], h);
			head = (head + 1) & SKB_RECYCLE_MAX_SHARED_POOLS_MASK;
			glob_recycler.head = head;
			spin_unlock(&glob_recycler.lock);
			/* We have refilled the CPU pool - dequeue */
			skb = __skb_dequeue(h);
		}
	}
#endif
	local_irq_restore(flags);
	put_cpu_var(recycle_list);

	if (likely(skb)) {
		struct skb_shared_info *shinfo;

		/* We're about to write a large amount to the skb to
		 * zero most of the structure so prefetch the start
		 * of the shinfo region now so it's in the D-cache
		 * before we start to write that.
		 */
		shinfo = skb_shinfo(skb);
		prefetchw(shinfo);

		zero_struct(skb, offsetof(struct sk_buff, tail));
		atomic_set(&skb->users, 1);
		skb->mac_header = (typeof(skb->mac_header))~0U;
		skb->transport_header = (typeof(skb->transport_header))~0U;
		zero_struct(shinfo, offsetof(struct skb_shared_info, dataref));
		atomic_set(&shinfo->dataref, 1);

		skb->data = skb->head + NET_SKB_PAD;
		skb_reset_tail_pointer(skb);

		skb->dev = dev;
	}

	if (skb)
		ubnt_memleak_handler("alloc", skb, length);
	return skb;
}

static inline bool skb_recycler_consume_success(struct sk_buff *skb, struct sk_buff_head *h, unsigned long flags)
{
	__skb_queue_head(h, skb);
	local_irq_restore(flags);
	preempt_enable();
	ubnt_memleak_handler("free", skb, 0);
	return true;
}

__attribute__ ((optimize(3))) static inline bool skb_recycler_consume(struct sk_buff *skb)
{
	unsigned long flags;
	struct sk_buff_head *h;

	/* Can we recycle this skb?  If not, simply return that we cannot */
	if (unlikely(!consume_skb_can_recycle(skb, SKB_RECYCLE_MIN_SIZE,
					      SKB_RECYCLE_MAX_SIZE)))
		return false;

	/* If we can, then it will be much faster for us to recycle this one
	 * later than to allocate a new one from scratch.
	 */
	h = &get_cpu_var(recycle_list);
	local_irq_save(flags);
	/* Attempt to enqueue the CPU hot recycle list first */
	if (likely(skb_queue_len(h) < skb_recycle_max_skbs)) {
		return skb_recycler_consume_success(skb, h, flags);
	}
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	h = this_cpu_ptr(&recycle_spare_list);

	/* The CPU hot recycle list was full; if the spare list is also full,
	 * attempt to move the spare list to the global list for other CPUs to
	 * use.
	 */
	if (unlikely(skb_queue_len(h) >= skb_recycle_spare_max_skbs)) {
		u8 cur_tail, next_tail;

		spin_lock(&glob_recycler.lock);
		cur_tail = glob_recycler.tail;
		next_tail = (cur_tail + 1) & SKB_RECYCLE_MAX_SHARED_POOLS_MASK;
		if (next_tail != glob_recycler.head) {
			struct sk_buff_head *p = &glob_recycler.pool[cur_tail];

			/* Move SKBs from CPU pool to Global pool*/
			skb_queue_splice_init(h, p);

			/* Done with global list init */
			glob_recycler.tail = next_tail;
			spin_unlock(&glob_recycler.lock);

			/* We have now cleared room in the spare;
			 * Initialize and enqueue skb into spare
			 */
			return skb_recycler_consume_success(skb, h, flags);
		}
		/* We still have a full spare because the global is also full */
		spin_unlock(&glob_recycler.lock);
	} else {
		/* We have room in the spare list; enqueue to spare list */
		return skb_recycler_consume_success(skb, h, flags);
	}
#endif

	local_irq_restore(flags);
	preempt_enable();

	return false;
}

static inline void skb_recycler_free_skb(struct sk_buff_head *list)
{
	struct sk_buff *skb = NULL;

	while ((skb = skb_dequeue(list)) != NULL) {
		skb_release_data(skb);
		kfree_skbmem(skb);
	}
}

static inline int skb_cpu_callback(struct notifier_block *nfb,
			    unsigned long action, void *ocpu)
{
	unsigned long oldcpu = (unsigned long)ocpu;

	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) {
		skb_recycler_free_skb(&per_cpu(recycle_list, oldcpu));
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
		spin_lock(&glob_recycler.lock);
		skb_recycler_free_skb(&per_cpu(recycle_spare_list, oldcpu));
		spin_unlock(&glob_recycler.lock);
#endif
	}

	return NOTIFY_OK;
}

#ifdef CONFIG_SKB_RECYCLER_PREALLOC
static int __init skb_prealloc_init_list(void)
{
	int i;
	struct sk_buff *skb;

	for (i = 0; i < SKB_RECYCLE_MAX_PREALLOC_SKBS; i++) {
		skb = __alloc_skb(SKB_RECYCLE_MAX_SIZE + NET_SKB_PAD,
				  GFP_KERNEL, 0, NUMA_NO_NODE);
		if (unlikely(!skb))
			return -ENOMEM;

		skb_reserve(skb, NET_SKB_PAD);

		skb_recycler_consume(skb);
	}
	return 0;
}
#endif

/* procfs: count
 * Show skb counts
 */
static int proc_skb_count_show(struct seq_file *seq, void *v)
{
	int cpu;
	int len;
	int total;
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	unsigned int i;
#endif

	total = 0;

	for_each_online_cpu(cpu) {
		len = skb_queue_len(&per_cpu(recycle_list, cpu));
		seq_printf(seq, "recycle_list[%d]: %d\n", cpu, len);
		total += len;
	}

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	for_each_online_cpu(cpu) {
		len = skb_queue_len(&per_cpu(recycle_spare_list, cpu));
		seq_printf(seq, "recycle_spare_list[%d]: %d\n", cpu, len);
		total += len;
	}

	spin_lock(&glob_recycler.lock);
	for (i = 0; i < SKB_RECYCLE_MAX_SHARED_POOLS; i++) {
		len = skb_queue_len(&glob_recycler.pool[i]);
		seq_printf(seq, "global_list[%d]: %d\n", i, len);
		total += len;
	}
	spin_unlock(&glob_recycler.lock);
#endif

	seq_printf(seq, "total: %d\n", total);
	return 0;
}

static int proc_skb_count_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_skb_count_show, PDE_DATA(inode));
}

static const struct file_operations proc_skb_count_fops = {
	.owner   = THIS_MODULE,
	.open    = proc_skb_count_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* procfs: flush
 * Flush skbs
 */
static void skb_recycler_flush_task(struct work_struct *work)
{
	unsigned long flags;
	struct sk_buff_head *h;
	struct sk_buff_head tmp;

	skb_queue_head_init(&tmp);

	h = &get_cpu_var(recycle_list);
	local_irq_save(flags);
	skb_queue_splice_init(h, &tmp);
	local_irq_restore(flags);
	put_cpu_var(recycle_list);
	skb_recycler_free_skb(&tmp);

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	h = &get_cpu_var(recycle_spare_list);
	local_irq_save(flags);
	skb_queue_splice_init(h, &tmp);
	local_irq_restore(flags);
	put_cpu_var(recycle_spare_list);
	skb_recycler_free_skb(&tmp);
#endif
}

static ssize_t proc_skb_flush_write(struct file *file,
				    const char __user *buf,
				    size_t count,
				    loff_t *ppos)
{
#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	unsigned int i;
	unsigned long flags;
#endif
	schedule_on_each_cpu(&skb_recycler_flush_task);

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	spin_lock_irqsave(&glob_recycler.lock, flags);
	for (i = 0; i < SKB_RECYCLE_MAX_SHARED_POOLS; i++)
		skb_recycler_free_skb(&glob_recycler.pool[i]);
	glob_recycler.head = 0;
	glob_recycler.tail = 0;
	spin_unlock_irqrestore(&glob_recycler.lock, flags);
#endif
	return count;
}

static const struct file_operations proc_skb_flush_fops = {
	.owner   = THIS_MODULE,
	.write   = proc_skb_flush_write,
	.open    = simple_open,
	.llseek  = noop_llseek,
};

/* procfs: max_skbs
 * Show max skbs
 */
static int proc_skb_max_skbs_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%d\n", skb_recycle_max_skbs);
	return 0;
}

static int proc_skb_max_skbs_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_skb_max_skbs_show, PDE_DATA(inode));
}

static ssize_t proc_skb_max_skbs_write(struct file *file,
				       const char __user *buf,
				       size_t count,
				       loff_t *ppos)
{
	int ret;
	int max;
	char buffer[13];

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count) != 0)
		return -EFAULT;
	ret = kstrtoint(strstrip(buffer), 10, &max);
	if (ret == 0 && max >= 0)
		skb_recycle_max_skbs = max;

	return count;
}

static const struct file_operations proc_skb_max_skbs_fops = {
	.owner   = THIS_MODULE,
	.open    = proc_skb_max_skbs_open,
	.read    = seq_read,
	.write   = proc_skb_max_skbs_write,
	.release = single_release,
};

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
/* procfs: max_spare_skbs
 * Show max spare skbs
 */
static int proc_skb_max_spare_skbs_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%d\n", skb_recycle_spare_max_skbs);
	return 0;
}

static int proc_skb_max_spare_skbs_open(struct inode *inode, struct file *file)
{
	return single_open(file,
			   proc_skb_max_spare_skbs_show,
			   PDE_DATA(inode));
}

static ssize_t
proc_skb_max_spare_skbs_write(struct file *file,
			      const char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	int ret;
	int max;
	char buffer[13];

	memset(buffer, 0, sizeof(buffer));
	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;
	if (copy_from_user(buffer, buf, count) != 0)
		return -EFAULT;
	ret = kstrtoint(strstrip(buffer), 10, &max);
	if (ret == 0 && max >= 0)
		skb_recycle_spare_max_skbs = max;

	return count;
}

static const struct file_operations proc_skb_max_spare_skbs_fops = {
	.owner   = THIS_MODULE,
	.open    = proc_skb_max_spare_skbs_open,
	.read    = seq_read,
	.write   = proc_skb_max_spare_skbs_write,
	.release = single_release,
};
#endif /* CONFIG_SKB_RECYCLER_MULTI_CPU */

static void skb_recycler_init_procfs(void)
{
	proc_net_skbrecycler = proc_mkdir("skb_recycler", init_net.proc_net);
	if (!proc_net_skbrecycler) {
		pr_err("cannot create skb_recycle proc dir");
		return;
	}

	if (!proc_create("count",
			 S_IRUGO,
			 proc_net_skbrecycler,
			 &proc_skb_count_fops))
		 pr_err("cannot create proc net skb_recycle held\n");

	if (!proc_create("flush",
			 S_IWUGO,
			 proc_net_skbrecycler,
			 &proc_skb_flush_fops))
		pr_err("cannot create proc net skb_recycle flush\n");

	if (!proc_create("max_skbs",
			 S_IRUGO | S_IWUGO,
			 proc_net_skbrecycler,
			 &proc_skb_max_skbs_fops))
		pr_err("cannot create proc net skb_recycle max_skbs\n");

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	if (!proc_create("max_spare_skbs",
			 S_IRUGO | S_IWUGO,
			 proc_net_skbrecycler,
			 &proc_skb_max_spare_skbs_fops))
		pr_err("cannot create proc net skb_recycle max_spare_skbs\n");
#endif
}

static void __init skb_recycler_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		skb_queue_head_init(&per_cpu(recycle_list, cpu));
	}

#ifdef CONFIG_SKB_RECYCLER_MULTI_CPU
	for_each_possible_cpu(cpu) {
		skb_queue_head_init(&per_cpu(recycle_spare_list, cpu));
	}

	spin_lock_init(&glob_recycler.lock);
	unsigned int i;

	for (i = 0; i < SKB_RECYCLE_MAX_SHARED_POOLS; i++)
		skb_queue_head_init(&glob_recycler.pool[i]);
	glob_recycler.head = 0;
	glob_recycler.tail = 0;
#endif

#ifdef CONFIG_SKB_RECYCLER_PREALLOC
	if (skb_prealloc_init_list())
		pr_err("Failed to preallocate SKBs for recycle list\n");
#endif

	hotcpu_notifier(skb_cpu_callback, 0);
	skb_recycler_init_procfs();
}
#endif /* CONFIG_SKB_RECYCLER */
