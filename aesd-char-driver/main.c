/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

#include "aesd-circular-buffer.h"

MODULE_AUTHOR("Arjav Garg");
MODULE_LICENSE("Dual BSD/GPL");

#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
	struct cdev* cdevptr;
	struct aesd_dev* aesd_device;
    PDEBUG("open");
        cdevptr	= inode->i_cdev;
        aesd_device = container_of(cdevptr, struct aesd_dev, cdev);
	aesd_device->working_buffer_entry.size = 0;
	aesd_device->working_buffer_entry.buffptr = NULL;
	filp->private_data = aesd_device;	
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
	struct cdev* cdevptr;
	struct aesd_dev* aesd_device;
    PDEBUG("release");	
        cdevptr	= inode->i_cdev;
        aesd_device = container_of(cdevptr, struct aesd_dev, cdev);
	if(aesd_device->working_buffer_entry.size) kfree(aesd_device->working_buffer_entry.buffptr);
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
	struct aesd_dev *mdevptr;
	size_t into_entry_nchars;
	struct aesd_buffer_entry *buffentry;
	size_t tocopy;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

        mdevptr	= filp->private_data;
	
	mutex_lock(&mdevptr->buff_mut);
	
	buffentry = aesd_circular_buffer_find_entry_offset_for_fpos(&mdevptr->circ_buffer, *f_pos, &into_entry_nchars);
	if(!buffentry)
	{
		retval = 0;
		PDEBUG("it is empty\n");
		goto finish_read;
	}

        tocopy = count < buffentry->size ? count : buffentry->size;
	if(retval = copy_to_user(buf, buffentry->buffptr, tocopy))
	{
		PDEBUG("copy to user returned: %lld\n", tocopy);
		retval = -EFAULT;
		goto finish_read;
	}
	*f_pos += tocopy;
	retval = tocopy;

finish_read: mutex_unlock(&mdevptr->buff_mut);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval;
	struct aesd_dev *mdevptr; 
	struct aesd_buffer_entry buffer_entry;
	char* copyarea;
	struct aesd_buffer_entry current_buffer;
	int probe;
	struct aesd_buffer_entry new_working_entry;
    retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
        mdevptr	= filp->private_data;

	mutex_lock(&mdevptr->buff_mut);

	/*if(buf[count-1] != '\n')
	{
		PDEBUG("Ignoring since last char is not '\\n'\n");
		retval = 0;
		goto finish_write;
	}*/

	copyarea = kmalloc(count, GFP_KERNEL);
	if(!copyarea)
	{
		retval = -ENOMEM;
		goto finish_write;
	}

	copy_from_user(copyarea, buf, count);

	retval=0;
	current_buffer.buffptr = copyarea;
	current_buffer.size = 0;
	probe = -1;
	while(++probe < count/sizeof(char))
	{	
		// keep track of local changes
		current_buffer.size+=sizeof(char);
		if(copyarea[probe] == '\n')
		{
			//attach local changes to working entry and flush.
			struct aesd_buffer_entry newbuffer_entry;
			newbuffer_entry.size = current_buffer.size + mdevptr->working_buffer_entry.size;
			newbuffer_entry.buffptr = kmalloc(newbuffer_entry.size, GFP_KERNEL);
			if(!newbuffer_entry.buffptr)
			{
				//nothing lost, nothing gained, let's pretend this never happened.
				goto finish_buffering;
			}

			memcpy(newbuffer_entry.buffptr, mdevptr->working_buffer_entry.buffptr, mdevptr->working_buffer_entry.size);
			memcpy(newbuffer_entry.buffptr+mdevptr->working_buffer_entry.size, current_buffer.buffptr, current_buffer.size);
			aesd_circular_buffer_add_entry(&mdevptr->circ_buffer, &newbuffer_entry);
			
			if(mdevptr->working_buffer_entry.size) kfree(mdevptr->working_buffer_entry.buffptr);
			mdevptr->working_buffer_entry.size = 0;
			current_buffer.buffptr += current_buffer.size;
			current_buffer.size = 0;
		}
		retval += sizeof(char);
	}

	// leftover local buffer needs to be added to global buffer	
	new_working_entry.buffptr = NULL;
	new_working_entry.size = mdevptr->working_buffer_entry.size + current_buffer.size;
	if(new_working_entry.size) new_working_entry.buffptr = kmalloc(new_working_entry.size, GFP_KERNEL);
	if(!new_working_entry.buffptr)
	{
		// local buffer content cannot be saved right now, it needs to be sent again
		retval -= current_buffer.size;
		goto finish_buffering;
	}
	if(mdevptr->working_buffer_entry.size)
	{
		memcpy(new_working_entry.buffptr, mdevptr->working_buffer_entry.buffptr, mdevptr->working_buffer_entry.size);
		kfree(mdevptr->working_buffer_entry.buffptr);
	}
	if(current_buffer.size)
	{
		memcpy(new_working_entry.buffptr+mdevptr->working_buffer_entry.size, current_buffer.buffptr, current_buffer.size);
	}
	mdevptr->working_buffer_entry = new_working_entry;
	
finish_buffering: kfree(copyarea);
finish_write: mutex_unlock(&mdevptr->buff_mut);
	      if(retval > 0)
	      {
		      *f_pos += retval;
	      }
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
	aesd_device.working_buffer_entry.size = 0;
	mutex_init(&aesd_device.buff_mut);
	aesd_circular_buffer_init(&aesd_device.circ_buffer);
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
