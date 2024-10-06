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

#include "aesd_ioctl.h"
#include "aesdchar.h"


int aesd_major = 0;// use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("flemingpatel");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    if (!dev)
    {
        return -EFAULT;
    }

    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    // noop - since it is per-device
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t offset_in_entry;
    size_t bytes_to_copy;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &offset_in_entry);
    if (!entry)
    {
        // no data available
        retval = 0;
        goto out;
    }

    bytes_to_copy = min(count, entry->size - offset_in_entry);
    if (copy_to_user(buf, entry->buffptr + offset_in_entry, bytes_to_copy))
    {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = count;
    struct aesd_dev *dev = filp->private_data;
    char *kbuf = NULL;
    size_t processed = 0;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
    {
        retval = -ENOMEM;
        goto out_unlock;
    }

    if (copy_from_user(kbuf, buf, count))
    {
        retval = -EFAULT;
        goto out_free;
    }

    while (processed < count)
    {
        char *newline_ptr;
        size_t chunk_size;
        struct aesd_buffer_entry entry;
        char *entry_buffer;
        size_t partial_size = dev->partial_size;

        // search for newline character
        newline_ptr = memchr(kbuf + processed, '\n', count - processed);
        if (newline_ptr)
        {
            // calculate the size up to and including the newline
            chunk_size = newline_ptr - (kbuf + processed) + 1;
        } else
        {
            // no newline found; process the rest of the buffer
            chunk_size = count - processed;
        }

        // allocate memory for the entry (existing partial + new chunk)
        entry_buffer = kmalloc(partial_size + chunk_size, GFP_KERNEL);
        if (!entry_buffer)
        {
            retval = -ENOMEM;
            goto out_free;
        }

        // copy existing partial data if any
        if (partial_size > 0)
        {
            memcpy(entry_buffer, dev->partial_buffer, partial_size);
            kfree(dev->partial_buffer);
            dev->partial_buffer = NULL;
            dev->partial_size = 0;
        }

        // copy the new chunk into the entry buffer
        memcpy(entry_buffer + partial_size, kbuf + processed, chunk_size);

        if (newline_ptr)
        {
            // newline found; complete entry and add to circular buffer
            entry.buffptr = entry_buffer;
            entry.size = partial_size + chunk_size;

            if (dev->buffer.full)
            {
                // free the memory of the overwritten entry
                kfree(dev->buffer.entry[dev->buffer.out_offs].buffptr);
            }

            aesd_circular_buffer_add_entry(&dev->buffer, &entry);
        } else
        {
            // no newline; store as partial data
            dev->partial_buffer = entry_buffer;
            dev->partial_size = partial_size + chunk_size;
        }

        processed += chunk_size;
    }

out_free:
    kfree(kbuf);
out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t new_pos;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    loff_t total_size = 0;
    int i;

    PDEBUG("llseek");

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // calculate total size of data in the cb
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, i)
    {
        total_size += entry->size;
    }

    switch (whence)
    {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = total_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > total_size)
    {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos;
    mutex_unlock(&dev->lock);
    return new_pos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t new_pos = 0;
    int i;
    struct aesd_buffer_entry *entry;
    int count = 0;
    int ret = 0;

    PDEBUG("ioctl");

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;

    if (cmd != AESDCHAR_IOCSEEKTO)
        return -ENOTTY;

    if (copy_from_user(&seekto, (const void __user *) arg, sizeof(seekto)))
        return -EFAULT;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // calculate the new position based on write_cmd and write_cmd_offset
    if (seekto.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        ret = -EINVAL;
        goto out;
    }

    // calculate new_pos by iterating through the circular buffer
    i = dev->buffer.out_offs;
    new_pos = 0;

    do
    {
        entry = &dev->buffer.entry[i];
        if (entry->buffptr == NULL)
            break;

        if (count == seekto.write_cmd)
        {
            if (seekto.write_cmd_offset >= entry->size)
            {
                ret = -EINVAL;
                goto out;
            }
            new_pos += seekto.write_cmd_offset;
            break;
        }

        new_pos += entry->size;
        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        count++;
    } while (i != dev->buffer.out_offs);

    if (count != seekto.write_cmd)
    {
        ret = -EINVAL;
        goto out;
    }

    filp->f_pos = new_pos;

out:
    mutex_unlock(&dev->lock);
    return ret;
}

struct file_operations aesd_fops = {
        .owner = THIS_MODULE,
        .open = aesd_open,
        .release = aesd_release,
        .read = aesd_read,
        .write = aesd_write,
        .llseek = aesd_llseek,
        .unlocked_ioctl = aesd_unlocked_ioctl
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.partial_buffer = NULL;
    aesd_device.partial_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;
    struct aesd_buffer_entry *entry;

    cdev_del(&aesd_device.cdev);

    mutex_lock(&aesd_device.lock);

    // free all entries in the circular buffer
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index)
    {
        if (entry->buffptr)
            kfree(entry->buffptr);
    }

    // free any remaining partial buffer
    if (aesd_device.partial_buffer)
        kfree(aesd_device.partial_buffer);

    mutex_unlock(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
