#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wesley C. Jones");
MODULE_DESCRIPTION("A simple example Linux module.");
MODULE_VERSION("0.01");

#define DEVICE_NAME "jh2_csprng"

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static int major_num;
static int device_open_count = 0;

static struct device_attribute dev_attr_mode = {
    .attr = {
        .name = "mode",
        .mode = 0644},
    .show = NULL,
    .store = NULL};

dev_t devNo;          // Major and Minor device numbers combined into 32 bits
struct class *pClass; // class_create will set this
/* This structure points to all of the device functions */
static struct file_operations file_ops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release}; /* When a process reads from our device, this gets called. */

void inline permute_box1(uint64_t *x1, uint64_t *x2, uint64_t *x3, uint64_t *x4)
{
    *x4 ^= (((*x1) << 32) ^ ((*x4) >> 32)) * (*x1) + 1;
    *x3 ^= (((*x2) << 24) ^ ((*x3) >> 16)) * (*x2) + 1;
    *x2 ^= (((*x3) << 32) ^ ((*x2) >> 32)) * (*x3) + 1;
    *x1 ^= (((*x4) << 16) ^ ((*x1) >> 24)) * (*x4) + 1;
}
void inline permute_box2(uint64_t *x1, uint64_t *x2, uint64_t *x3, uint64_t *x4)
{
    *x4 ^= (((*x1) << 32) ^ ((*x4) >> 24)) + 1;
    *x3 ^= (((*x2) << 24) ^ ((*x3) >> 32)) + 1;
    *x2 ^= (((*x3) << 32) ^ ((*x2) >> 24)) + 1;
    *x1 ^= (((*x4) << 24) ^ ((*x1) >> 32)) + 1;
}
void inline permute_box3(uint64_t *x1, uint64_t *x2, uint64_t *x3, uint64_t *x4)
{
    *x1 ^= ((*x1) * (*x2) ^ (*x3) * (*x4)) + 1;
    *x2 ^= ((*x1) * (*x2) ^ (*x3) * (*x4)) + 1;
    *x3 ^= ((*x1) * (*x2) ^ (*x3) * (*x4)) + 1;
    *x4 ^= ((*x1) * (*x2) ^ (*x3) * (*x4)) + 1;
}

static void inline compute_jhash(uint8_t *buffer)
{
    uint64_t state_1 = 0;
    uint64_t state_2 = 0;
    uint64_t state_3 = 0;
    uint64_t state_4 = 0;
    uint8_t j = 0;
    uint8_t i = 0;
    for (i = 0; i < 32; i += 4, j++)
    {
        state_1 ^= ((uint64_t)(buffer[31 - i - 3])) << (j * 8);
        state_2 ^= ((uint64_t)(buffer[31 - i - 2])) << (j * 8);
        state_3 ^= ((uint64_t)(buffer[31 - i - 1])) << (j * 8);
        state_4 ^= ((uint64_t)(buffer[31 - i - 0])) << (j * 8);
        permute_box1(&state_1, &state_2, &state_3, &state_4);
        permute_box2(&state_1, &state_2, &state_3, &state_4);
        permute_box3(&state_1, &state_2, &state_3, &state_4);
    }
    memcpy(buffer, &state_1, 8);
    memcpy(buffer + 8, &state_2, 8);
    memcpy(buffer + 16, &state_3, 8);
    memcpy(buffer + 24, &state_4, 8);
}

// Return 64 bits of entropy for prng
static inline uint64_t get_entropy(void)
{
    return random_get_entropy();
}

static ssize_t device_read(struct file *flip, char *buffer, size_t len, loff_t *offset)
{
    static uint8_t state[32] = {0};
    static uint8_t seeded = 0;
    size_t i;
    int bytes_read = 0;
    int remainder = len % 32;
    if (!seeded)
    {
        uint64_t seed = get_entropy();
        memcpy(state, &seed, 8);
        seeded = 1;
    }
    if (len >= 32)
    {
        int iter = len / 32;
        for (i = 0; i < iter; i++)
        {
            compute_jhash(state);
            copy_to_user(buffer + bytes_read, state, 32);
            bytes_read += 32;
        }
        if (remainder > 0)
        {
            compute_jhash(state);
            copy_to_user(buffer + bytes_read, state, remainder);
            bytes_read += remainder;
        }

        return bytes_read;
    }
    compute_jhash(state);
    copy_to_user(buffer + bytes_read, state, remainder);
    bytes_read = remainder;

    return bytes_read;
} /* Called when a process tries to write to our device */

static ssize_t device_write(struct file *flip, const char *buffer, size_t len, loff_t *offset)
{
    /* This is a read-only device */
    printk(KERN_ALERT "This operation is not supported.\n");
    return -EINVAL;
} /* Called when a process opens our device */

static int device_open(struct inode *inode, struct file *file)
{
    /* If device is open, return busy */
    if (device_open_count)
    {
        return -EBUSY;
    }
    device_open_count++;
    try_module_get(THIS_MODULE);
    return 0;
} /* Called when a process closes our device */

static int device_release(struct inode *inode, struct file *file)
{
    /* Decrement the open counter and usage count. Without this, the module would not unload. */
    device_open_count--;
    module_put(THIS_MODULE);
    return 0;
}

static char *tty_devnode(struct device *dev, umode_t *mode)
{
    if (!mode)
        return NULL;
    if (dev->devt == MKDEV(major_num, 0) ||
        dev->devt == MKDEV(major_num, 2))
        *mode = 0666;
    return NULL;
}

static int __init lkm_example_init(void)
{
    struct device *pDev;
    /* Try to register character device */

    // Register character device
    major_num = register_chrdev(0, DEVICE_NAME, &file_ops);
    if (major_num < 0)
    {
        printk(KERN_ALERT "Could not register device: %d\n", major_num);
        return major_num;
    }
    devNo = MKDEV(major_num, 0); // Create a dev_t, 32 bit version of numbers

    // Create /sys/class/kmem in preparation of creating /dev/kmem
    pClass = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(pClass))
    {
        printk(KERN_WARNING "\ncan't create class");
        unregister_chrdev_region(devNo, 1);
        return -ECANCELED;
    }
    pClass->devnode = tty_devnode;
    // Create /dev/kmem for this char dev
    if (IS_ERR(pDev = device_create(pClass, NULL, devNo, NULL, DEVICE_NAME)))
    {
        printk(KERN_WARNING "lkm_example.ko can't create device %s\n", DEVICE_NAME);
        class_destroy(pClass);
        unregister_chrdev_region(devNo, 1);
        return -ECANCELED;
    }
    return 0;
}

static void __exit lkm_example_exit(void)
{
    /* Remember — we have to clean up after ourselves. Unregister the character device. */
    device_destroy(pClass, devNo);
    class_unregister(pClass);
    class_destroy(pClass);
    unregister_chrdev(major_num, DEVICE_NAME);
    printk(KERN_INFO "Goodbye, World !\n");
} /* Register module functions */

module_init(lkm_example_init);
module_exit(lkm_example_exit);