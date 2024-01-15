#include <linux/rbtree.h>
#include "eviction_tracker.h"

struct eviction_tracker_node {
	// Reference to the eviction tracker that this node belongs to
	// We need this to access the eviction policy from the compare_eviction_tracker_nodes function
	struct eviction_tracker *eviction_tracker;
	struct rb_node rb_node;
	struct inode *inode;
};

static bool compare_nodes(struct rb_node *rb_node1,
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

static dev_t get_device_id_from_inode(struct inode *inode)
{
	return inode->i_sb->s_dev;
}

struct eviction_tracker *
get_eviction_tracker(struct eviction_policy *eviction_policy)
{
	// TODO use better allocator
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

// TODO: Replace by an addition hashmap (inode_id -> eviction_tracker_node)
static struct eviction_tracker_node *
get_eviction_tracker_node_by_inode_id(struct eviction_tracker *eviction_tracker,
				      int inode_id)
{
	struct eviction_tracker_node *iter_node, *iter_node2;
	rbtree_postorder_for_each_entry_safe(iter_node, iter_node2,
					     &eviction_tracker->root, rb_node) {
		if (iter_node->inode->i_ino == inode_id) {
			return iter_node;
		}
	}

	return NULL;
}

void update_inode_in_eviction_tracker(struct eviction_tracker *eviction_tracker,
				      struct inode *inode)
{
	struct eviction_tracker_node *node =
		get_eviction_tracker_node_by_inode_id(eviction_tracker,
						      inode->i_ino);

	if (!node) {
		printk(KERN_INFO "inode %lu not found in eviction tracker\n",
		       inode->i_ino);
		return;
	}

	// For now just remove and re-add the inode to the eviction tracker (should be reasonably efficient when we add hashing)
	rb_erase(&node->rb_node, &eviction_tracker->root);
	rb_add(&node->rb_node, &eviction_tracker->root, compare_nodes);
}

void remove_inode_from_eviction_tracker(
	struct eviction_tracker *eviction_tracker, struct inode *inode)
{
	struct eviction_tracker_node *node =
		get_eviction_tracker_node_by_inode_id(eviction_tracker,
						      inode->i_ino);

	if (!node) {
		printk(KERN_INFO "inode %lu not found in eviction tracker\n",
		       inode->i_ino);
		return;
	}

	rb_erase(&node->rb_node, &eviction_tracker->root);
	kfree(node);
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

	rb_add(&new_node->rb_node, &eviction_tracker->root, compare_nodes);
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