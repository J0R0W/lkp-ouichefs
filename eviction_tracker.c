#include <linux/rbtree.h>
#include "eviction_tracker.h"

struct eviction_tracker_node {
	// Reference to the eviction tracker that this node belongs to
	// We need this to access the eviction policy from the compare_eviction_tracker_nodes function
	struct eviction_tracker *eviction_tracker;
	struct rb_node rb_node;
	struct inode *inode;
};

struct eviction_tracker *
get_eviction_tracker(struct eviction_policy *eviction_policy)
{
	struct eviction_tracker *eviction_tracker =
		kmalloc(sizeof(struct eviction_tracker), GFP_KERNEL);
	eviction_tracker->root = RB_ROOT;
	eviction_tracker->eviction_policy = eviction_policy;
	mutex_init(&eviction_tracker->lock);
	kref_init(&eviction_tracker->refcount);

	// Pass ownership of eviction_tracker to caller
	kref_get(&eviction_tracker->refcount);
	return eviction_tracker;
}

void release_eviction_tracker(struct kref *refcount)
{
	struct eviction_tracker *eviction_tracker =
		container_of(refcount, struct eviction_tracker, refcount);
	mutex_destroy(&eviction_tracker->lock);

	// Clear eviction tracker
	struct rb_node *rb_node = rb_first(&eviction_tracker->root);

	while (rb_node != NULL) {
		rb_erase(rb_node, &eviction_tracker->root);
		kfree(rb_entry(rb_node, struct eviction_tracker_node, rb_node));
		rb_node = rb_first(&eviction_tracker->root);
	}

	printk(KERN_INFO "destroyed eviction tracker\n");
}

static bool compare_eviction_tracker_nodes(struct rb_node *rb_node1,
					   const struct rb_node *rb_node2)
{
	struct eviction_tracker_node *node1 =
		rb_entry(rb_node1, struct eviction_tracker_node, rb_node);
	struct eviction_tracker_node *node2 =
		rb_entry(rb_node2, struct eviction_tracker_node, rb_node);

	struct eviction_policy *eviction_policy =
		node1->eviction_tracker->eviction_policy;

	return eviction_policy->compare(node1->inode, node2->inode) < 0;
}

bool add_inode_to_eviction_tracker(struct eviction_tracker *eviction_tracker,
				   struct inode *inode, bool check_if_exists)
{
	if (check_if_exists) {
		struct eviction_tracker_node *iter_node, *iter_node2;
		rbtree_postorder_for_each_entry_safe(iter_node, iter_node2,
						     &eviction_tracker->root,
						     rb_node) {
			if (iter_node->inode == inode) {
				printk(KERN_INFO
				       "inode %lu already in eviction tracker\n",
				       inode->i_ino);
				return false;
			}
		}
	}

	struct eviction_tracker_node *new_node =
		kmalloc(sizeof(struct eviction_tracker_node), GFP_KERNEL);

	if (!new_node) {
		printk(KERN_INFO "failed to allocate memory for new node\n");
		return false;
	}

	new_node->inode = inode;

	rb_add(&new_node->rb_node, &eviction_tracker->root,
	       compare_eviction_tracker_nodes);
	new_node->eviction_tracker = eviction_tracker;
	printk(KERN_INFO "added inode %lu to eviction tracker\n", inode->i_ino);

	return true;
}

struct inode *get_inode_to_evict(struct eviction_tracker *eviction_tracker)
{
	struct rb_node *rb_node = rb_first(&eviction_tracker->root);
	if (!rb_node) {
		printk(KERN_INFO "eviction tracker is empty\n");
		return NULL;
	}

	struct eviction_tracker_node *node =
		rb_entry(rb_node, struct eviction_tracker_node, rb_node);
	struct inode *inode = node->inode;

	rb_erase(&node->rb_node, &eviction_tracker->root);
	kfree(node);

	printk(KERN_INFO "removed inode %lu from eviction tracker\n",
	       inode->i_ino);

	return inode;
}