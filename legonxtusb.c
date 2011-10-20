/*
	Lego NXT USB driver v 1.0.0.1

	
	Copyright (C) 2006 	Mykola Konyk (subs@konyk.org)
	
	GPL License

	Based on USB Skeleton driver - 2.0
 	Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
*/
	

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>


// Define these values to match your devices
#define LEGONXT_USB_VENDOR_ID		0x0694
#define LEGONXT_USB_PRODUCT_ID		0x0002

// Get a minor for our device (Major 180, Minor 192)
#define USB_LEGONXT_MINOR_BASE		192

// Bulk read timeout in ms (lego usb guarantees us 25ms)
#define LEGONXT_USB_READ_TIMEOUT	25

// Additional defines
#define MAX_TRANSFER			( PAGE_SIZE - 512 )
#define WRITES_IN_FLIGHT		8

// Function prototypes
static ssize_t legonxt_usb_read(struct file *file, char *buffer, size_t count, loff_t *ppos);
static ssize_t legonxt_usb_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos);
static int legonxt_usb_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void legonxt_usb_disconnect(struct usb_interface *interface);

static void legonxt_usb_delete(struct kref *kref);
static int legonxt_usb_open(struct inode *inode, struct file *file);
static int legonxt_usb_release(struct inode *inode, struct file *file);

static void legonxt_usb_write_bulk_callback(struct urb *urb, struct pt_regs *regs);

// Table of devices that work with this driver
static struct usb_device_id legonxt_usb_table [] = {
	{ USB_DEVICE(LEGONXT_USB_VENDOR_ID, LEGONXT_USB_PRODUCT_ID) },
	{ }	
};

// Register our device table
MODULE_DEVICE_TABLE(usb, legonxt_usb_table);


// Structure to hold all of our device specific stuff
struct usb_skel 
{
	// The usb device for this device 
	struct usb_device *	udev;	
	
	// The interface for this device 
	struct usb_interface *	interface;

	// Limiting the number of writes in progress 
	struct semaphore	limit_sem;		

	// The buffer to receive data 
	unsigned char *		bulk_in_buffer;

	// The size of the receive buffer 		
	size_t			bulk_in_size;	

	// The address of the bulk in endpoint 
	__u8			bulk_in_endpointAddr;	
	
	// The address of the bulk out endpoint 
	__u8			bulk_out_endpointAddr;	

	// Reference counter
	struct kref		kref;
};

// Helper macro
#define to_skel_dev(d) container_of(d, struct usb_skel, kref)

// Protype, actual definition almost at the end
//static struct usb_driver legonxt_usb_driver;

// Driver definition structure - specifies probe/disconnect functions and driver table
// which specifies which devices this driver is servicing
static struct usb_driver legonxt_usb_driver = 
{
	.name =		"legonxtusb",
	.probe =	legonxt_usb_probe,
	.disconnect =	legonxt_usb_disconnect,
	.id_table =	legonxt_usb_table,
};

// Delete helper
static void legonxt_usb_delete(struct kref *kref)
{	
	struct usb_skel *dev = to_skel_dev(kref);

	usb_put_dev(dev->udev);

	kfree (dev->bulk_in_buffer);
	kfree (dev);
}

// Open helper
static int legonxt_usb_open(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&legonxt_usb_driver, subminor);

	if(!interface) 
	{
		err ("%s - error, can't find device for minor %d",
		     __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	
	if(!dev)
	{
		retval = -ENODEV;
		goto exit;
	}

	// Increment reference count
	kref_get(&dev->kref);

	// Save our object in the file's private structure
	file->private_data = dev;

exit:
	return(retval);
}

// Release helper
static int legonxt_usb_release(struct inode *inode, struct file *file)
{
	struct usb_skel *dev;

	dev = (struct usb_skel *)file->private_data;
	
	if(!dev)
	{
		return -ENODEV;
	}

	// decrement the reference count
	kref_put(&dev->kref, legonxt_usb_delete);
	return(0);
}

// Called whenever userspace application starts reading from /dev/legonxtN
static ssize_t legonxt_usb_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval = 0;
	int bytes_read;

	dev = (struct usb_skel *)file->private_data;
	
	//info("USB read %d bytes!", count);

	// Perform a blocking bulk read, timeout in LEGONXT_USB_READ_TIMEOUT ms
	retval = usb_bulk_msg(dev->udev,
			      usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
			      dev->bulk_in_buffer,
			      min(dev->bulk_in_size, count),
				&bytes_read, LEGONXT_USB_READ_TIMEOUT);

	// If bytes were read, copy to userspace
	if(!retval) 
	{
		if(copy_to_user(buffer, dev->bulk_in_buffer, bytes_read))
			retval = -EFAULT;
		else
			retval = bytes_read;
	}

	return(retval);
}

// Callback function for bulk write
static void legonxt_usb_write_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_skel *dev;

	dev = (struct usb_skel *)urb->context;

	// sync/async unlink faults aren't errors, no really.. 
	if (urb->status && 
	    !(urb->status == -ENOENT || 
	      urb->status == -ECONNRESET ||
	      urb->status == -ESHUTDOWN)) {
		dbg("%s - nonzero write bulk status received: %d",
		    __FUNCTION__, urb->status);
	}

	// Release allocated buffer
	usb_buffer_free(urb->dev, urb->transfer_buffer_length, 
			urb->transfer_buffer, urb->transfer_dma);

	up(&dev->limit_sem);
}

// Called whenever userspace application writes bytes to /dev/legonxtN
static ssize_t legonxt_usb_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);
	
	dev = (struct usb_skel *)file->private_data;

	// Log number of bytes
	//info("USB write %d bytes!", count);
	
	// No data to write
	if(!count)
	{
		goto exit;
	}

	// Limit the number of URBs in flight to stop a user from using up all RAM
	if(down_interruptible(&dev->limit_sem)) 
	{
		retval = -ERESTARTSYS;
		goto exit;
	}

	// Create a urb, and a buffer for it, and copy the data to the urb
	urb = usb_alloc_urb(0, GFP_KERNEL);
	
	// Problem allocating URB
	if(!urb)
	{
		retval = -ENOMEM;
		goto error;
	}

	// Buffer allocation
	buf = usb_buffer_alloc(dev->udev, writesize, GFP_KERNEL, &urb->transfer_dma);
	
	// Problem allocating buffer
	if(!buf)
	{
		retval = -ENOMEM;
		goto error;
	}

	// Copy data from userspace and handle fail case
	if(copy_from_user(buf, user_buffer, writesize)) 
	{
		retval = -EFAULT;
		goto error;
	}

	// Initialize the urb properly
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			  buf, writesize, legonxt_usb_write_bulk_callback, dev);

	// Set URB flags
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	// Send the data out the bulk port
	retval = usb_submit_urb(urb, GFP_KERNEL);

	// Problem sending?
	if(retval)
	{
		err("%s - failed submitting write urb, error %d", __FUNCTION__, retval);
		goto error;
	}

	// Release URB, USB core will free it eventually on its own
	usb_free_urb(urb);

exit:
	// Return number of bytes written, if not done properly, fop-write can be 
	// stuck in a loop
	return(writesize);

// Handle whatever errors that may have occured..
error:
	usb_buffer_free(dev->udev, writesize, buf, urb->transfer_dma);
	usb_free_urb(urb);
	up(&dev->limit_sem);
	return(retval);
}

// Fop operations - userspace interaction
static struct file_operations legonxt_usb_fops = 
{
	.owner =	THIS_MODULE,
	.read =		legonxt_usb_read,
	.write =	legonxt_usb_write,
	.open =		legonxt_usb_open,
	.release =	legonxt_usb_release,
};

// USB class driver definition
static struct usb_class_driver legonxt_usb_class = 
{
	.name =		"legonxt%d",
	.fops =		&legonxt_usb_fops,
	.minor_base =	USB_LEGONXT_MINOR_BASE,
};

// Called whenever Lego NXT is attached
static int legonxt_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_skel *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	// Allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if(!dev)
	{
		err("Out of memory");
		goto error;
	}

	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	// Set up the endpoint information
	// Use only the first bulk-in and bulk-out endpoints
	// Remember we are using BULK transfer!
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
					== USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) 
		{
			// Found a bulk in endpoint!
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			
			if (!dev->bulk_in_buffer) 
			{
				err("Could not allocate bulk_in_buffer");
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
					== USB_DIR_OUT) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) 
		{
			// Found a bulk out endpoint!
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
	
	// No endpoints found, abort?
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		err("Could not find both bulk-in and bulk-out endpoints");
		goto error;
	}

	// Save our data pointer in this interface device 
	usb_set_intfdata(interface, dev);

	// Can register the device now, it is ready
	retval = usb_register_dev(interface, &legonxt_usb_class);
	if(retval)
	{
		// Cannot register driver - usually meaning minor is not set right
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	info("USB Skeleton device now attached to legonxt%d", interface->minor);
	return(0);

// Handle whatever errors that may have occured
error:
	if(dev)
	{
		kref_put(&dev->kref, legonxt_usb_delete);
	}

	return(retval);
}

// Called whenever Lego NXT device is disconnected
static void legonxt_usb_disconnect(struct usb_interface *interface)
{
	struct usb_skel *dev;
	int minor = interface->minor;

	// This is here to prevent racing condition between legonxt_usb_open()
	// and legonxt_usb_disconnect
	lock_kernel();

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	// Grab our minor
	usb_deregister_dev(interface, &legonxt_usb_class);

	// Unlock previously locked kernel
	unlock_kernel();

	// Increment usage count
	kref_put(&dev->kref, legonxt_usb_delete);

	info("USB legonxt #%d now disconnected", minor);
}

// Called whenever module is loaded
static int __init legonxt_usb_initialize(void)
{
	int result;

	// Register our USB driver with the USB core
	result = usb_register(&legonxt_usb_driver);
	if(result)
	{
		err("usb_register failed. Error number %d", result);
	}

	return(result);
}

// Called whenever module is unloaded
static void __exit legonxt_usb_finalize(void)
{
	// Deregister our USB driver with the USB core
	usb_deregister(&legonxt_usb_driver);
}

// Define intialization and finalization functions
module_init(legonxt_usb_initialize);
module_exit(legonxt_usb_finalize);

// Module License - GPL
MODULE_LICENSE("GPL");
