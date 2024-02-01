#include <linux/module.h>
#include <linux/fs.h>

#include "../eviction_tracker.h"

static int compare_largest_file(struct inode *inode1, struct inode *inode2)
{
	printk(KERN_INFO "my eviction_policy_compare called - success!\n");

	if (inode1->i_size > inode2->i_size)
		return 1;
	else if (inode1->i_size < inode2->i_size)
		return -1;
	else
		return 0;
}

struct eviction_policy my_eviction_policy = {
	.compare = compare_largest_file,
};

static int __init eviction_policy_changer_init(void)
{
	return eviction_tracker_change_policy(&my_eviction_policy);
}

static void __exit eviction_policy_changer_exit(void)
{
	eviction_tracker_change_policy(NULL);
}

module_init(eviction_policy_changer_init);
module_exit(eviction_policy_changer_exit);

MODULE_AUTHOR("Project Group 13");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
	"A demonstration of a module that changes the eviction policy of the ouichefs filesystem");