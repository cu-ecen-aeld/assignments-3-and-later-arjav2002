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
#include "aesd_ioctl.h"

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
	filp->private_data = aesd_device;	
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");	
    return 0;
}

loff_t aesd_llseek(struct file* filp, loff_t offset, int whence)
{
	struct aesd_dev *mdevptr;
	size_t buffsize;
	int i;
	PDEBUG("Seeking offset: %lld\twhence: %d\n", offset, whence);
	mdevptr = filp->private_data;

	// find size, then call fixed size llseek
	mutex_lock(&mdevptr->buff_mut);
	buffsize = 0;
	for(i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) buffsize += mdevptr->circ_buffer.entry[i].size;
	mutex_unlock(&mdevptr->buff_mut);

	return fixed_size_llseek(filp, offset, whence, buffsize);
}

long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset)
{
	struct aesd_dev *mdevptr;
	uint8_t i;
	mdevptr = filp->private_data;

	PDEBUG("Write_cmd: %d\tWritecmd offset: %d\n", write_cmd, write_cmd_offset);

	mutex_lock(&mdevptr->buff_mut);
	if(write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) return -EINVAL;

	PDEBUG("Size at %d is %d\n", write_cmd, mdevptr->circ_buffer.entry[write_cmd].size);
	if(mdevptr->circ_buffer.entry[write_cmd].size == 0
		|| write_cmd_offset >= mdevptr->circ_buffer.entry[write_cmd].size)
		return -EINVAL;

	PDEBUG("File offset was at: %d\n", filp->f_pos);
	filp->f_pos += write_cmd_offset;
	i = mdevptr->circ_buffer.out_offs;
	while(i != write_cmd)
	{
		filp->f_pos += mdevptr->circ_buffer.entry[i].size;
		i = (i+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
	mutex_unlock(&mdevptr->buff_mut);

	PDEBUG("Adjusted file offset to %d !\n", filp->f_pos);

	return 0;

}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case AESDCHAR_IOCSEEKTO:
		{
			struct aesd_seekto seekto;
			if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0)
			{
				PDEBUG("Could not copy from user in aesd_unlocked_ioctl\n");
				return -EFAULT;
			}
			return aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
			break;
		}
		default:
			PDEBUG("Command: %d not supported\n", cmd);
			return -EINVAL;

	}
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
	struct aesd_dev *mdevptr;
	size_t into_entry_nbytes;
	struct aesd_buffer_entry *buffentry;
	size_t tocopy;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

        mdevptr	= filp->private_data;
	
	mutex_lock(&mdevptr->buff_mut);
	
	buffentry = aesd_circular_buffer_find_entry_offset_for_fpos(&mdevptr->circ_buffer, *f_pos, &into_entry_nbytes);
	if(!buffentry)
	{
		retval = 0;
		PDEBUG("it is empty\n");
		goto finish_read;
	}

        tocopy = count < (buffentry->size - into_entry_nbytes) ? count : (buffentry->size - into_entry_nbytes);
	if(retval = copy_to_user(buf, buffentry->buffptr + into_entry_nbytes/sizeof(char), tocopy))
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

	PDEBUG("in_off is now: %d", mdevptr->circ_buffer.in_offs);
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

			//copy global and local into newbuffer and add it into the circular buffer
			memcpy(newbuffer_entry.buffptr, mdevptr->working_buffer_entry.buffptr, mdevptr->working_buffer_entry.size);
			memcpy(newbuffer_entry.buffptr+mdevptr->working_buffer_entry.size, current_buffer.buffptr, current_buffer.size);
			aesd_circular_buffer_add_entry(&mdevptr->circ_buffer, &newbuffer_entry);
			
			PDEBUG("in_off is now: %d", mdevptr->circ_buffer.in_offs);
			
			// clean up global and local buffers
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
	PDEBUG("new global size: %d\tcurrent global size: %d\tcurrent local size: %d\n", new_working_entry.size, mdevptr->working_buffer_entry.size, current_buffer.size);
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
	PDEBUG("new global size: %d\tcurrent global size: %d\tcurrent local size: %d\n", new_working_entry.size, mdevptr->working_buffer_entry.size, current_buffer.size);
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
    .llseek =	aesd_llseek,
    .unlocked_ioctl = 	aesd_unlocked_ioctl
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
	if(aesd_device.working_buffer_entry.size) kfree(aesd_device.working_buffer_entry.buffptr);
 

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
