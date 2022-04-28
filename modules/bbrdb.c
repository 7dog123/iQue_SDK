/*
 * USB BB RDB driver for debugging BBPLAYER over USB - 0.2
 *
 * Driver derived from USB Skeleton driver - 0.7
 *
 * Copyright (c) 2003 BroadOn Communications Corp
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 * History:
 *
 * 2003_05_15 - 0.1 - first version
 * 2003_07_01 - 0.2 - add support for select and non-blocking i/o
 * 
 */

#define __ASM_I386_PROCESSOR_H	/* XXX ghastly kludge */

#include <linux/module.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#undef __ASM_I386_PROCESSOR_H	/* XXX ghastly kludge */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_DEBUG
static int      debug = 1;
#else
static int      debug = 0;
#endif

/*
 * Use our own dbg macro 
 */
#undef dbg
#define dbg(format, arg...) do { if (debug) printk(KERN_INFO __FILE__ ": " format "\n" , ## arg); } while (0)

#ifndef MIN
#define MIN(x,y)	(((x)<(y))?(x):(y))
#endif
#define bcopy	local_bcopy

/*
 * Version Information 
 */
#define DRIVER_VERSION "v0.3"
#define DRIVER_AUTHOR "Rufus T. Firefly, rtf@freedonia.gov"
#define DRIVER_DESC "BB RDB Interface Driver for Linux (c)2003"

/*
 * Module paramaters 
 */
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

/*
 * Define these values to match your device 
 */
#define BBRDB_VENDOR_ID		0xbb3d
#define BBRDB_VENDOR_ID_NEW	0x1527
#define BBRDB_PRODUCT_ID	0xbbdb

/*
 * table of devices that work with this driver 
 */
static struct usb_device_id rdb_table[] = {
    {USB_DEVICE(BBRDB_VENDOR_ID_NEW, BBRDB_PRODUCT_ID)},
    {USB_DEVICE(BBRDB_VENDOR_ID, BBRDB_PRODUCT_ID)},
    {}				/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, rdb_table);

/*
 * Apparently unused range of minors on USB major 180 
 */
#define BBRDB_MINOR_BASE	160

/*
 * Up to this number of devices plugged in at once 
 */
#define MAX_DEVICES		8

/*
 * Structure to hold all the RDB device specific stuff
 *
 * Locking rules:
 *
 * Task level code uses the device semaphore to protect against
 * concurrent access by other tasks, which can happen even on
 * a uniprocessor.
 *
 * The spinlock is used to protect the flags and the read_urb
 * between concurrent accesses by task level code and interrupt
 * code.  There is no deadlock since task code grabs the
 * semaphore first and then the spinlock.  The interrupt code
 * never grabs the semaphore, of course.
 * 
 * On a uniprocessor, the spinlocks go away and RDB_LOCK()
 * just locks out the bottom half.
 *
 * Note that the read completion handler only modifies the
 * flags and does not actually shuffle read data around.
 */
struct usb_rdb {
    struct usb_device *udev;		/* save off the usb device pointer */
    struct usb_interface *interface;	/* the interface for this device */
    devfs_handle_t  devfs;	/* devfs device node */
    spinlock_t      lock;	/* lock for flags and read_urb */
    unsigned char   flags;	/* state flags */
    unsigned char   minor;	/* the starting minor number for this
				 * device */
    unsigned char   num_ports;	/* the number of ports this device has */
    char            num_interrupt_in;	/* number of interrupt in
					 * endpoints we have */
    char            num_bulk_in;	/* number of bulk in endpoints we
					 * have */
    char            num_bulk_out;	/* number of bulk out endpoints we 
					 * have */

    unsigned char  *intr_in_buffer;	/* the buffer to receive data */
    int             intr_in_size;	/* the size of the receive buffer */
    __u8            intr_in_endpointAddr;	/* the address of the intr 
						 * in endpoint */

    unsigned char  *bulk_in_buffer;	/* data available to read by user
					 * program */
    int             bulk_in_size;	/* the full size of the read
					 * buffers */
    int             bulk_in_valid;	/* current valid contents of read
					 * buffer */
    struct urb     *read_urb;		/* the urb used to recv data */
    unsigned char  *bulk_in_curbuf;	/* buffer pointed to by read_urb */
    __u8            bulk_in_endpointAddr;	/* the address of the bulk 
						 * in endpoint */

    unsigned char  *bulk_out_buffer;	/* the buffer to send data */
    int             bulk_out_size;	/* the size of the send buffer */
    struct urb     *write_urb;		/* the urb used to send data */
    __u8            bulk_out_endpointAddr;	/* the address of the bulk 
						 * out endpoint */

    int             open_count;	/* number of times this port has been
				 * opened */
    __u8            setup[8];	/* setup packet */
    struct semaphore sem;	/* locks this structure */
    struct completion iowait;	/* wait here for write urb to complete */
    wait_queue_head_t wait;	/* for poll/select on read urb */
};

/*
 * the global usb devfs handle 
 */
extern devfs_handle_t usb_devfs_handle;

/*
 * bit values for usb_rdb flags field 
 */
#define	FLAGS_READ_ACTIVE	0x0001
#define	FLAGS_DATA_AVAIL	0x0002
#define	FLAGS_READ_URB_FULL	0x0004
#define	FLAGS_READ_ERROR	0x0008
#define	FLAGS_WRITE_ACTIVE	0x0010

/*
 * local function prototypes 
 */
static ssize_t  rdb_read(struct file *file, char *buffer, size_t count,
			 loff_t * ppos);
static ssize_t  rdb_write(struct file *file, const char *buffer,
			  size_t count, loff_t * ppos);
static unsigned int rdb_poll(struct file *file,
			     struct poll_table_struct *wait);
static int      rdb_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg);
static int      rdb_open(struct inode *inode, struct file *file);
static int      rdb_release(struct inode *inode, struct file *file);

static void    *rdb_probe(struct usb_device *dev, unsigned int ifnum,
			  const struct usb_device_id *id);
static void     rdb_disconnect(struct usb_device *dev, void *ptr);

static void     rdb_read_bulk_callback(struct urb *urb);
static void     rdb_write_bulk_callback(struct urb *urb);
static void     rdb_post_read(struct usb_rdb *dev);

/*
 * array of pointers to our devices that are currently connected 
 */
static struct usb_rdb *minor_table[MAX_DEVICES];

/*
 * lock to protect the minor_table structure 
 */
static          DECLARE_MUTEX(minor_table_mutex);

#define RDB_LOCK()	spin_lock_irqsave(&dev->lock, irqmask)
#define RDB_UNLOCK()	spin_unlock_irqrestore(&dev->lock, irqmask)

/*
 * File operations needed when we register this driver.
 * This assumes that this driver NEEDS file operations,
 * of course, which means that the driver is expected
 * to have a node in the /dev directory. If the USB
 * device were for a network interface then the driver
 * would use "struct net_driver" instead, and a serial
 * device would use "struct tty_driver". 
 */
static struct file_operations rdb_fops = {
    /*
     * The owner field is part of the module-locking
     * mechanism. The idea is that the kernel knows
     * which module to increment the use-counter of
     * BEFORE it calls the device's open() function.
     * This also means that the kernel can decrement
     * the use-counter again before calling release()
     * or should the open() function fail.
     *
     * Not all device structures have an "owner" field
     * yet. "struct file_operations" and "struct net_device"
     * do, while "struct tty_driver" does not. If the struct
     * has an "owner" field, then initialize it to the value
     * THIS_MODULE and the kernel will handle all module
     * locking for you automatically. Otherwise, you must
     * increment the use-counter in the open() function
     * and decrement it again in the release() function
     * yourself.
     */
    owner: THIS_MODULE,

    read:    rdb_read,
    write:   rdb_write,
    poll:    rdb_poll,
    ioctl:   rdb_ioctl,
    open:    rdb_open,
    release: rdb_release,
};


/*
 * usb specific object needed to register this driver with the usb
 * subsystem 
 */
static struct usb_driver rdb_driver = {
    name:     "bbrdb",
    probe:     rdb_probe,
    disconnect:rdb_disconnect,
    fops:     &rdb_fops,
    minor:     BBRDB_MINOR_BASE,
    id_table:  rdb_table,
};

#if 0
/**
 *	usb_rdb_debug_data
 */
static inline void
usb_rdb_debug_data(const char *function, int size,
		   const unsigned char *data)
{
    int             i;

    if (!debug)
	return;

    printk(KERN_INFO __FILE__ ": %s - length = %d, data = ",
	   function, size);
    for (i = 0; i < size; ++i) {
	printk("%.2x ", data[i]);
    }
    printk("\n");
}
#endif

/*
 * Simplistic version to avoid problems with misaligned and
 * overlapping data in the system memcpy routines.
 */
void
local_bcopy(char *fp, char *tp, int n)
{
    while (n-- > 0)
	*tp++ = *fp++;
}

/**
 *	rdb_delete
 */
static inline void
rdb_delete(struct usb_rdb *dev)
{
    minor_table[dev->minor] = NULL;
    if (dev->bulk_in_buffer != NULL)
	kfree(dev->bulk_in_buffer);
    if (dev->bulk_in_curbuf != NULL)
	kfree(dev->bulk_in_curbuf);
    if (dev->read_urb != NULL)
	usb_free_urb(dev->read_urb);
    if (dev->bulk_out_buffer != NULL)
	kfree(dev->bulk_out_buffer);
    if (dev->write_urb != NULL)
	usb_free_urb(dev->write_urb);
    kfree(dev);
}

/**
 *	rdb_open
 */
static int
rdb_open(struct inode *inode, struct file *file)
{
    struct usb_rdb *dev = NULL;
    int             subminor;
    int             rv = 0;

    dbg("rdb_open");

    subminor = MINOR(inode->i_rdev) - BBRDB_MINOR_BASE;
    dbg("rdb_open: minor %d\n", subminor);
    if ((subminor < 0) || (subminor >= MAX_DEVICES)) {
	return -ENODEV;
    }

    /*
     * lock minor table and get local data for this minor 
     */
    down(&minor_table_mutex);
    dev = minor_table[subminor];
    if (dev == NULL) {
	up(&minor_table_mutex);
	return -ENODEV;
    }

    /*
     * lock this device 
     */
    down(&dev->sem);

    /*
     * unlock the minor table 
     */
    up(&minor_table_mutex);

    /*
     * this is an exclusive open device 
     */
    if (dev->open_count > 0) {
	up(&dev->sem);
	return -EBUSY;
    }

    /*
     * increment usage count for the driver 
     */
    ++dev->open_count;

    /*
     * save object in the file's private structure 
     */
    file->private_data = dev;

    /*
     * reinitialize the private data structures for this subminor 
     */
    dev->bulk_in_valid = 0;
    dev->read_urb->actual_length = 0;
    dev->flags = 0;

    /*
     * start asynchronous read right away 
     */
    rdb_post_read(dev);

    /*
     * unlock this device 
     */
    up(&dev->sem);

    return rv;
}


/**
 *	rdb_release
 */
static int
rdb_release(struct inode *inode, struct file *file)
{
    struct usb_rdb *dev;
    int             rv = 0;
    unsigned long   irqmask;

    dev = (struct usb_rdb *) file->private_data;
    if (dev == NULL) {
	dbg("rdb_release - object is NULL");
	return -ENODEV;
    }

    dbg("rdb_release - minor %d openct %d", dev->minor, dev->open_count);

    /*
     * lock minor table 
     */
    down(&minor_table_mutex);

    /*
     * lock this device 
     */
    down(&dev->sem);

    if (dev->open_count <= 0) {
	dbg("rdb_release - device not opened");
	rv = -ENODEV;
	goto exit_not_opened;
    }

    /*
     * Cancel any pending urbs
     */
    RDB_LOCK();
    if (dev->flags & FLAGS_READ_ACTIVE) {
        RDB_UNLOCK();
	usb_unlink_urb(dev->read_urb);
	/*
	 * This is gross, but sleep/wakeup doesn't work and building a
	 * race-free version isn't worth it.
	 */
	while (1) {
            RDB_LOCK();
            if (!(dev->flags & FLAGS_READ_ACTIVE)) {
                RDB_UNLOCK();
		break;
            }	
            RDB_UNLOCK();
	    yield();
	}
    } else {
        RDB_UNLOCK();
    }
    /* this is probably unnecessary, but does no harm */
    usb_unlink_urb(dev->write_urb);

    /*
     * decrement usage count for the device 
     */
    if (--dev->open_count < 0)
	dev->open_count = 0;

    if (dev->udev == NULL) {
	/*
	 * the device was unplugged before the file was released 
	 */
	up(&dev->sem);
	rdb_delete(dev);
	up(&minor_table_mutex);
	return 0;
    }

exit_not_opened:
    up(&dev->sem);
    up(&minor_table_mutex);

    return rv;
}

/**
 *	rdb_write_bulk_callback
 */
static void
rdb_write_bulk_callback(struct urb *urb)
{
    struct usb_rdb *dev = (struct usb_rdb *) urb->context;

    dbg("rdb_write_bulk_callback - minor %d", dev->minor);

    if (urb->status != 0) {
	dbg("rdb_write_bulk_callback - nonzero write bulk status received: %d",
	     urb->status);
    }

    complete(&dev->iowait);
    return;
}

/**
 *	rdb_read_bulk_callback
 */
static void
rdb_read_bulk_callback(struct urb *urb)
{
    struct usb_rdb *dev = (struct usb_rdb *) urb->context;
    int             len;
    unsigned long   irqmask;

    dbg("rdb_read_bulk_callback - minor %d got len %d", dev->minor, urb->actual_length);

    if (urb->status != 0) {
	dbg("rdb_read_bulk_callback - nonzero read bulk status received: %d",
	     urb->status);
    }

    RDB_LOCK();
    dev->flags &= ~FLAGS_READ_ACTIVE;
    if (urb->status == 0 && urb->actual_length > 0)
	dev->flags |= (FLAGS_DATA_AVAIL | FLAGS_READ_URB_FULL);
    else if (urb->status != 0)
	dev->flags |= FLAGS_READ_ERROR;
    RDB_UNLOCK();

    /*
     * sleepers awake! 
     */
    wake_up_interruptible(&dev->wait);

    return;
}


/**
 *	rdb_post_read - start asynchronous read
 */
static void
rdb_post_read(struct usb_rdb *dev)
{
    int             rv;
    unsigned long   irqmask;

    dbg("rdb_post_read - minor %d", dev->minor);

    /*
     * Called with device semaphore locked, which protects
     * against access by other tasks.
     *
     * Grab spinlock to protect flags word from interrupt
     * callbacks.
     */
    RDB_LOCK();
    if (dev->flags & FLAGS_READ_ACTIVE) {
	RDB_UNLOCK();
	err("rdb_post_read - read urb already submitted\n");
	return;
    }

    if (dev->flags & FLAGS_READ_URB_FULL) {
	RDB_UNLOCK();
	err("rdb_post_read - read urb still has unread data\n");
	return;
    }

    dev->flags |= FLAGS_READ_ACTIVE;
    RDB_UNLOCK();

    //memset(dev->read_urb, 0, sizeof(struct urb));
    FILL_BULK_URB(dev->read_urb, dev->udev,
		  usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
		  dev->bulk_in_curbuf,
		  dev->bulk_in_size, rdb_read_bulk_callback, dev);
    /*
     * Must set USB_ASYNC_UNLINK on this urb or else the cleanup
     * doesn't work properly at close time.
     */
    dev->read_urb->transfer_flags |= USB_ASYNC_UNLINK;
    dbg("rdb_post_read urb: pipe 0x%x tbuf 0x%x tlen %d actlen %d",
	dev->read_urb->pipe,
	dev->read_urb->transfer_buffer,
	dev->read_urb->transfer_buffer_length,
	dev->read_urb->actual_length);

    /*
     * submit the read urb 
     */
    rv = usb_submit_urb(dev->read_urb);
    if (rv) {
	err("rdb_post_read - failed submitting read urb, error %d", rv);
	RDB_LOCK();
	dev->flags &= ~FLAGS_READ_ACTIVE;
	dev->flags |= FLAGS_READ_ERROR;
	RDB_UNLOCK();
    }
}

/**
 *	rdb_read
 */
static ssize_t
rdb_read(struct file *file, char *buffer, size_t count, loff_t * ppos)
{
    struct usb_rdb *dev;
    struct urb     *urb;
    size_t          total,
                    rcount;
    int             loops;
    unsigned long   irqmask;

    dev = (struct usb_rdb *) file->private_data;

    dbg("rdb_read - minor %d, count = %d", dev->minor, count);

    loops = 0;
    total = 0;

    /*
     * lock the device 
     */
    down(&dev->sem);

    while (count > 0) {
	/*
	 * Safety catch 
	 */
	if (loops++ > 20) {
	    total = -ELOOP;
	    break;
	}

	/*
	 * verify that the device wasn't unplugged 
	 */
	if (dev->udev == NULL) {
	    total = -ENODEV;
	    break;
	}

	/*
	 * check for errors reported from lower level
	 */
	RDB_LOCK();
	if (dev->flags & FLAGS_READ_ERROR) {
	    RDB_UNLOCK();
	    total = -EIO;
	    break;
	}

	/*
	 * check first for pre-buffered data 
	 */
	if (dev->flags & FLAGS_DATA_AVAIL) {
	    RDB_UNLOCK();

	    rcount = MIN(count, dev->bulk_in_valid);
	    if (rcount > 0) {
		if (copy_to_user(buffer, dev->bulk_in_buffer, rcount)) {
		    total = -EFAULT;
		    break;
		}

		dev->bulk_in_valid -= rcount;
		count -= rcount;
		total += rcount;
		buffer += rcount;

		if (dev->bulk_in_valid > 0) {
		    bcopy(&dev->bulk_in_buffer[rcount],
			  dev->bulk_in_buffer, dev->bulk_in_valid);
		}

		if (count == 0)
		    break;
	    }

	    /*
	     * next check for a partially consumed read urb 
	     */
	    urb = dev->read_urb;
	    RDB_LOCK();
	    if (dev->flags & FLAGS_READ_URB_FULL) {
		if (dev->flags & FLAGS_READ_ACTIVE) {
		    RDB_UNLOCK();
		    err("rdb_read"
			" - read_urb pending, but still has data");
		    /*
		     * XXX can not happen ... what now? 
		     */
		} else
		    RDB_UNLOCK();

		rcount = MIN(count, urb->actual_length);
		if (rcount > 0) {
		    if (copy_to_user(buffer, urb->transfer_buffer, rcount)) {
			total = -EFAULT;
			break;
		    }

		    urb->actual_length -= rcount;
		    count -= rcount;
		    total += rcount;
		    buffer += rcount;
		    if (urb->actual_length > 0) {
			/*
			 * slide remaining data down to the front of urb
			 * buffer 
			 */
			bcopy(&dev->bulk_in_curbuf[rcount],
			      dev->bulk_in_curbuf, urb->actual_length);
		    }
		}

		if (urb->actual_length > 0) {
		    /*
		     * if we can free up the read_urb by moving data to
		     * the primary buffer, do it 
		     */
		    // ASSERT(dev->bulk_in_valid == 0);
		    rcount = dev->bulk_in_size - dev->bulk_in_valid;
		    if (rcount >= urb->actual_length) {
			bcopy(dev->bulk_in_curbuf,
			      &dev->bulk_in_buffer[dev->bulk_in_valid],
			      urb->actual_length);
			dev->bulk_in_valid += urb->actual_length;
			urb->actual_length = 0;
			dev->flags &= ~FLAGS_READ_URB_FULL;
		    }
		} else {
		    /*
		     * Ok to turn this off without holding spinlock, since we know
		     * urb is not active until rdb_post_read is called again
		     */
		    dev->flags &= ~FLAGS_READ_URB_FULL;
		}
	    }
	}

	RDB_LOCK();
	if (dev->bulk_in_valid == 0 && !(dev->flags & FLAGS_READ_URB_FULL)) {
	    dev->flags &= ~FLAGS_DATA_AVAIL;
	    /*
	     * data consumed, if no urb queued, start it 
	     */
	    if (!(dev->flags & FLAGS_READ_ACTIVE)) {
		RDB_UNLOCK();
		rdb_post_read(dev);
	    } else {
		RDB_UNLOCK();
	    }
	}

	if (count == 0)
	    break;

	/*
	 * if all available data has been consumed and read is blocking, wait 
	 */
	RDB_LOCK();
	if (!(dev->flags & FLAGS_DATA_AVAIL)) {
	    RDB_UNLOCK();
	    up(&dev->sem);
	    if (file->f_flags & O_NONBLOCK) {
		dbg("rdb_read - read would block, ret %d", total);
		return (total ? total : -EAGAIN);
	    }
	    dbg("rdb_read - blocking read sleeps");
#if 0
	    if (wait_event_interruptible
		(dev->wait, ((dev->flags & FLAGS_DATA_AVAIL))))
		return (total ? total : -EINTR);
#else
	    /*
	     * this is may be racey 
	     */
	    interruptible_sleep_on(&dev->wait);
	    dbg("rdb_read - blocking read awakened");
	    if (signal_pending(current))
		return (total ? total : -EINTR);
#endif
	} else {
	    RDB_UNLOCK();
	    up(&dev->sem);
	}

	down(&dev->sem);
    }

    if (total < 0)
	dbg("rdb_read - minor %d, returned error = %d", dev->minor,
	    total);
    else
	dbg("rdb_read - minor %d, returned bytes = %d", dev->minor,
	    total);
#if 0
    if (total > 0)
	usb_rdb_debug_data("rdb_read", total, dev->bulk_in_buffer);
#endif
    up(&dev->sem);
    return total;
}


/**
 *	rdb_write
 */
static ssize_t
rdb_write(struct file *file, const char *buffer, size_t count,
	  loff_t * ppos)
{
    struct usb_rdb *dev;
    ssize_t         wcount = 0;
    int             rv = 0;
    unsigned long   irqmask;

    dev = (struct usb_rdb *) file->private_data;

    dbg("rdb_write - minor %d, count = %d", dev->minor, count);

    /*
     * verify that we actually have some data to write 
     */
    if (count == 0) {
	dbg("rdb_write - write request of 0 bytes");
	return rv;
    }

    /*
     * lock this object 
     */
    down(&dev->sem);

    /*
     * see if we are already in the middle of a write 
     */
    RDB_LOCK();
    if (dev->flags & FLAGS_WRITE_ACTIVE) {
        RDB_UNLOCK();
	dbg("rdb_write - already writing");
	up(&dev->sem);
	return -EINPROGRESS;
    }
    RDB_UNLOCK();

    /*
     * we can only write as much as the output buffer
     * will hold at one time.  Repeat until done.
     */
    while (count > 0) {

	/*
	 * verify that the device wasn't unplugged 
	 */
	if (dev->udev == NULL) {
	    rv = -ENODEV;
	    break;
	}

	wcount = MIN(count, (size_t) dev->bulk_out_size);

	/*
	 * copy the data from userspace into the urb 
	 */
	if (copy_from_user(dev->write_urb->transfer_buffer,
			   buffer, wcount)) {
	    rv = -EFAULT;
	    break;
	}
#if 0
	usb_rdb_debug_data("rdb_write", wcount,
			   dev->write_urb->transfer_buffer);
#endif

	/*
	 * set up write urb 
	 */
	//memset(dev->write_urb, 0, sizeof(struct urb));
	FILL_BULK_URB(dev->write_urb, dev->udev,
		      usb_sndbulkpipe(dev->udev,
				      dev->bulk_out_endpointAddr),
		      dev->write_urb->transfer_buffer, wcount,
		      rdb_write_bulk_callback, dev);
	/*
	 * Make sure we send zero length packet at the end of
	 * each full buffer, so the BB can recognize EOT even
	 * when the size works out to be an even multiple of 64.
	 */
	if ((wcount >= 64) && ((wcount & 63) == 0))
	    dev->write_urb->transfer_flags |= USB_ZERO_PACKET;
	else
	    dev->write_urb->transfer_flags &= ~USB_ZERO_PACKET;

	/*
	 * send the data out the bulk port 
	 */
	rv = usb_submit_urb(dev->write_urb);
	if (rv) {
	    err("rdb_write - failed submitting write urb, error %d",
		rv);
	    break;
	} else {
	    rv = wcount;
	}

	/*
	 * Wait for urb to complete
	 */
        RDB_LOCK();
        dev->flags |= FLAGS_WRITE_ACTIVE;
        RDB_UNLOCK();
	up(&dev->sem);

	wait_for_completion(&dev->iowait);

	down(&dev->sem);
        RDB_LOCK();
        dev->flags &= ~FLAGS_WRITE_ACTIVE;
        RDB_UNLOCK();

	if (dev->write_urb->status != 0) {
	    err("rdb_write"
		" - nonzero write bulk status %d recvd on write of %d",
		dev->write_urb->status, wcount);
	    rv = dev->write_urb->status;
	    break;
	}

	count -= wcount;
	buffer += wcount;
    }

    /*
     * unlock the device 
     */
    up(&dev->sem);
    return rv;
}

/**
 *	rdb_poll
 */
static unsigned int
rdb_poll(struct file *file, struct poll_table_struct *wait)
{
    struct usb_rdb *dev;
    unsigned int    rv = 0;
    unsigned long   irqmask;

    dev = (struct usb_rdb *) file->private_data;

    poll_wait(file, &dev->wait, wait);

    /*
     * if the device was unplugged, report an exception 
     */
    down(&dev->sem);
    if (dev->udev == NULL) {
	rv |= POLLERR;
    }
    up(&dev->sem);

    /*
     * If there is data available to be consumed, return read ready
     *
     * Note that the spinlock is probably superfluous here, since
     * DATA_AVAIL can be turned on only by interrupt code and turned
     * off only by task code.
     */
    RDB_LOCK();
    if (dev->flags & (FLAGS_DATA_AVAIL|FLAGS_READ_ERROR))
	rv |= (POLLIN | POLLRDNORM);
    RDB_UNLOCK();

    return rv;
}

/**
 *	rdb_ioctl
 */
static int
rdb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
    struct usb_rdb *dev;

    dev = (struct usb_rdb *) file->private_data;

    /*
     * lock this object 
     */
    down(&dev->sem);

    /*
     * verify that the device wasn't unplugged 
     */
    if (dev->udev == NULL) {
	up(&dev->sem);
	return -ENODEV;
    }

    dbg("rdb_ioctl - minor %d, cmd 0x%.4x, arg %ld",
	dev->minor, cmd, arg);

    /*
     * fill in device specific stuff here 
     */

    /*
     * unlock the device 
     */
    up(&dev->sem);

    /*
     * return that we did not understand this ioctl call 
     */
    return -ENOTTY;
}

/**
 *	rdb_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static void    *
rdb_probe(struct usb_device *udev, unsigned int ifnum,
	  const struct usb_device_id *id)
{
    struct usb_rdb *dev = NULL;
    struct usb_interface *interface;
    struct usb_interface_descriptor *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    struct urb     *urb;
    __u32           defpipe;
    int             minor;
    int             buffer_size;
    int             i;
    int             ret;
    char            name[10];


    /*
     * See if the device offered us matches what we can accept 
     */
    if (((udev->descriptor.idVendor != BBRDB_VENDOR_ID) &&
         (udev->descriptor.idVendor != BBRDB_VENDOR_ID_NEW)) ||
	(udev->descriptor.idProduct != BBRDB_PRODUCT_ID)) {
	return NULL;
    }
    dbg("bbrdb_probe: RDB device found");

    /*
     * select a "subminor" number (part of a minor number) 
     */
    down(&minor_table_mutex);
    for (minor = 0; minor < MAX_DEVICES; ++minor) {
	if (minor_table[minor] == NULL)
	    break;
    }
    if (minor >= MAX_DEVICES) {
	info("Too many devices plugged in, can not handle this device.");
	goto exit;
    }

    /*
     * allocate memory for device state and intialize it 
     */
    dev = kmalloc(sizeof(struct usb_rdb), GFP_KERNEL);
    if (dev == NULL) {
	err("Out of memory");
	goto exit;
    }
    memset(dev, 0x00, sizeof(*dev));
    minor_table[minor] = dev;

    interface = &udev->actconfig->interface[ifnum];

    spin_lock_init(&dev->lock);
    init_MUTEX(&dev->sem);
    init_completion(&dev->iowait);
    init_waitqueue_head(&dev->wait);
    dev->udev = udev;
    dev->interface = interface;
    dev->minor = minor;

    /*
     * set up the endpoint information 
     */
    /*
     * check out the endpoints 
     */
    iface_desc = &interface->altsetting[0];
    for (i = 0; i < iface_desc->bNumEndpoints; ++i) {
	endpoint = &iface_desc->endpoint[i];

	if ((endpoint->bEndpointAddress & 0x80) &&
	    ((endpoint->bmAttributes & 3) == 0x02)) {
	    /*
	     * found a bulk in endpoint 
	     */
	    dev->read_urb = usb_alloc_urb(0);
	    if (!dev->read_urb) {
		err("No free urbs available");
		goto error;
	    }
	    // buffer_size = endpoint->wMaxPacketSize;
	    buffer_size = 2048;
	    dev->bulk_in_size = buffer_size;
	    dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
	    dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
	    if (!dev->bulk_in_buffer) {
		err("Couldn't allocate bulk_in_buffer");
		goto error;
	    }
	    dev->bulk_in_curbuf = kmalloc(buffer_size, GFP_KERNEL);
	    if (!dev->bulk_in_curbuf) {
		err("Couldn't allocate bulk_in_curbuf");
		goto error;
	    }
	    dbg("bbrdb_probe: bulk in EP %d, blksize %d",
		dev->bulk_in_endpointAddr, dev->bulk_in_size);
	}

	if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
	    ((endpoint->bmAttributes & 3) == 0x02)) {
	    /*
	     * found a bulk out endpoint 
	     */
	    dev->write_urb = usb_alloc_urb(0);
	    if (!dev->write_urb) {
		err("No free urbs available");
		goto error;
	    }
	    // buffer_size = endpoint->wMaxPacketSize;
	    buffer_size = 2048;
	    dev->bulk_out_size = buffer_size;
	    dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
	    dev->bulk_out_buffer = kmalloc(buffer_size, GFP_KERNEL);
	    if (!dev->bulk_out_buffer) {
		err("Couldn't allocate bulk_out_buffer");
		goto error;
	    }
	    dbg("bbrdb_probe: bulk out EP %d, blksize %d",
		dev->bulk_out_endpointAddr, dev->bulk_out_size);
	    FILL_BULK_URB(dev->write_urb, udev,
			  usb_sndbulkpipe(udev,
					  endpoint->bEndpointAddress),
			  dev->bulk_out_buffer, buffer_size,
			  rdb_write_bulk_callback, dev);
	}
    }

    /*
     * initialize the devfs node for this device and register it 
     */
    sprintf(name, "bbrdb%d", dev->minor);

    dev->devfs = devfs_register(usb_devfs_handle, name,
				DEVFS_FL_DEFAULT, USB_MAJOR,
				BBRDB_MINOR_BASE + dev->minor,
				S_IFCHR | S_IRUSR | S_IWUSR |
				S_IRGRP | S_IWGRP | S_IROTH,
				&rdb_fops, NULL);

    /*
     * let the user know what node this device is now attached to 
     */
    dbg("BB RDB device now attached to bbrdb%d", dev->minor);
    goto exit;

  error:
    rdb_delete(dev);
    dev = NULL;

  exit:
    up(&minor_table_mutex);
    return dev;
}


/**
 *	rdb_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 */
static void
rdb_disconnect(struct usb_device *udev, void *ptr)
{
    struct usb_rdb *dev;
    int             minor;

    dev = (struct usb_rdb *) ptr;

    down(&minor_table_mutex);
    down(&dev->sem);

    minor = dev->minor;

    /*
     * remove the devfs node 
     */
    devfs_unregister(dev->devfs);

    /*
     * if the device is not opened, then we clean up right now 
     */
    if (!dev->open_count) {
	up(&dev->sem);
	rdb_delete(dev);
    } else {
	dev->udev = NULL;
	up(&dev->sem);
    }

    dbg("BB RDB #%d now disconnected", minor);
    up(&minor_table_mutex);
}


/**
 *	usb_rdb_init
 */
static int __init
usb_rdb_init(void)
{
    int             result;

    /*
     * register this driver with the USB subsystem 
     */
    result = usb_register(&rdb_driver);
    if (result < 0) {
	err("usb_register failed for the " __FILE__
	    " driver. Error number %d", result);
	return -1;
    }

    info(DRIVER_DESC " " DRIVER_VERSION);
    return 0;
}


/**
 *	usb_rdb_exit
 */
static void __exit
usb_rdb_exit(void)
{
    /*
     * deregister this driver with the USB subsystem 
     */
    usb_deregister(&rdb_driver);
}

module_init(usb_rdb_init);
module_exit(usb_rdb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
#if defined(MODULE_LICENSE)
MODULE_LICENSE("GPL");
#endif
