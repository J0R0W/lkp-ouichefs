#include <linux/hashtable.h>
#include <linux/list.h>
#include "eviction_tracker.h"

// TODO: MUTEX LOCKING!!!

struct eviction_tracker {
	// ID of the device that this eviction_tracker tracks
	dev_t device_id;
	// Eviction policy that is used to order the inodes
	struct eviction_policy *eviction_policy;
	// Head of the list that contains all inodes
	struct list_head inode_list;
	// Hashmap that maps device IDs to eviction trackers
	// so with a given device_id we can find the eviction tracker that tracks that device (or if the device is being tracked at all)
	// The key is device_id
	struct hlist_node registered_device_hashtable_node;
	// Lock that protects the eviction tracker
	struct mutex inode_list_lock;
};

struct eviction_tracker_node {
	// Inode that is tracked by this node
	struct inode *inode;
	// Node that is used to order the inodes
	struct list_head inode_list_head;
};

static DEFINE_MUTEX(registered_devices_lock);
// Allows efficient operations for up to 256 registered devices
DEFINE_READ_MOSTLY_HASHTABLE(registered_devices, 8);

static dev_t _get_device_id_from_inode(struct inode *inode)
{
	return inode->i_sb->s_dev;
}

static struct eviction_tracker *
_get_eviction_tracker_from_device_id(dev_t device_id)
{
	// TODO: More efficient lookup
	struct eviction_tracker *eviction_tracker;
	int bucket;
	hash_for_each(registered_devices, bucket, eviction_tracker,
		      registered_device_hashtable_node) {
		if (eviction_tracker->device_id == device_id) {
			return eviction_tracker;
		}
	}

	printk(KERN_INFO "device %d is not registered\n", device_id);
	return ERR_PTR(-ENODEV);
}

static struct eviction_tracker *
_get_eviction_tracker_from_inode(struct inode *inode)
{
	dev_t device_id = _get_device_id_from_inode(inode);

	return _get_eviction_tracker_from_device_id(device_id);
}

static struct eviction_tracker_node *
_get_eviction_tracker_node_from_inode(struct inode *inode)
{
	dev_t device_id = _get_device_id_from_inode(inode);

	struct eviction_tracker *eviction_tracker =
		_get_eviction_tracker_from_device_id(device_id);

	if (IS_ERR(eviction_tracker)) {
		printk(KERN_INFO "device %d is not registered\n", device_id);
		return ERR_PTR(-ENODEV);
	}

	struct eviction_tracker_node *iter_node;
	list_for_each_entry(iter_node, &eviction_tracker->inode_list,
			    inode_list_head) {
		if (iter_node->inode->i_ino == inode->i_ino) {
			return iter_node;
		}
	}

	printk(KERN_INFO "inode %lu not found in eviction tracker\n",
	       inode->i_ino);
	return ERR_PTR(-ENOENT);
}

int eviction_tracker_register_device(dev_t device_id,
				     struct eviction_policy *eviction_policy)
{
	// TODO: Check if device is already registered (then device_id is already a key in the hashtable)
	// then print error and return
	int bucket;
	struct eviction_tracker *eviction_tracker;
	hash_for_each(registered_devices, bucket, eviction_tracker,
		      registered_device_hashtable_node) {
		if (eviction_tracker->device_id == device_id) {
			printk(KERN_INFO "device %d is already registered\n",
			       device_id);
			return -EEXIST;
		}
	}

	// TODO: Use slab allocator
	struct eviction_tracker *new_tracker =
		kmalloc(sizeof(struct eviction_tracker), GFP_KERNEL);

	if (!new_tracker) {
		printk(KERN_INFO "failed to allocate memory for new tracker\n");
		return -ENOMEM;
	}

	// Initialize eviction_tracker
	new_tracker->device_id = device_id;
	new_tracker->eviction_policy = eviction_policy;
	INIT_HLIST_NODE(&new_tracker->registered_device_hashtable_node);
	INIT_LIST_HEAD(&new_tracker->inode_list);
	mutex_init(&new_tracker->inode_list_lock);

	hash_add(registered_devices,
		 &new_tracker->registered_device_hashtable_node, device_id);

	return 0;
}

int eviction_tracker_unregister_device(dev_t device_id)
{
	struct eviction_tracker *eviction_tracker =
		_get_eviction_tracker_from_device_id(device_id);

	if (IS_ERR(eviction_tracker)) {
		printk(KERN_INFO "device %d is not registered\n", device_id);
		return -ENOENT;
	}

	//TODO: Remove device from hashtable and free the eviction_tracker
	hash_del(&eviction_tracker->registered_device_hashtable_node);

	// Clear inode list
	struct eviction_tracker_node *iter_node, *iter_node2;

	list_for_each_entry_safe(iter_node, iter_node2,
				 &eviction_tracker->inode_list,
				 inode_list_head) {
		list_del(&iter_node->inode_list_head);
		kfree(iter_node);
	}

	kfree(eviction_tracker);

	return 0;
}

int eviction_tracker_remove_inode(struct inode *inode)
{
	struct eviction_tracker *eviction_tracker =
		_get_eviction_tracker_from_inode(inode);

	printk(KERN_INFO
	       "eviction_tracker is not unused because we access it here: %p\n",
	       eviction_tracker);

	struct eviction_tracker_node *node =
		_get_eviction_tracker_node_from_inode(inode);

	if (IS_ERR(node)) {
		printk(KERN_INFO "inode %lu not found in eviction tracker\n",
		       inode->i_ino);
		return -ENOENT;
	}

	list_del(&node->inode_list_head);
	kfree(node);

	return 0;
}

int eviction_tracker_add_inode(struct inode *inode)
{
	struct eviction_tracker *eviction_tracker =
		_get_eviction_tracker_from_inode(inode);

	if (IS_ERR(eviction_tracker)) {
		printk(KERN_INFO "device %d is not registered\n",
		       _get_device_id_from_inode(inode));
		return -ENODEV;
	}

	struct eviction_tracker_node *new_node =
		kmalloc(sizeof(struct eviction_tracker_node), GFP_KERNEL);

	if (!new_node) {
		printk(KERN_INFO "failed to allocate memory for new node\n");
		return -ENOMEM;
	}

	new_node->inode = inode;

	list_add_tail(&new_node->inode_list_head,
		      &eviction_tracker->inode_list);

	return 0;
}

struct inode *eviction_tracker_get_inode_for_eviction(dev_t device_id)
{
	struct eviction_tracker *eviction_tracker =
		_get_eviction_tracker_from_device_id(device_id);

	if (IS_ERR(eviction_tracker)) {
		printk(KERN_INFO "device %d is not registered\n", device_id);
		return ERR_PTR(-ENODEV);
	}

	struct eviction_policy *eviction_policy =
		eviction_tracker->eviction_policy;

	struct eviction_tracker_node *max_node = list_first_entry_or_null(
		&eviction_tracker->inode_list, struct eviction_tracker_node,
		inode_list_head);

	if (!max_node) {
		printk(KERN_INFO "inode list is empty\n");
		return ERR_PTR(-ENOENT);
	}

	struct eviction_tracker_node *iter_node;
	list_for_each_entry(iter_node, &eviction_tracker->inode_list,
			    inode_list_head) {
		if (eviction_policy->compare(max_node->inode,
					     iter_node->inode) > 0) {
			max_node = iter_node;
		}
	}

	return max_node->inode;
}