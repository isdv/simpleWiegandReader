#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("isdv");
MODULE_DESCRIPTION("A Simple wiegand reader.");

// Wiegand D0 Pin
#define D0_GPIO 1
// Wiegand D1 Pin
#define D1_GPIO 0
//after 500msec card num cleared
#define LAST_CARD_TIMEOUT 1000
#define WIEGAND_BIT_TIMEOUT  50


//
#define D0_GPIO_NAME "wiegand_reader_D0"
#define D1_GPIO_NAME "wiegand_reader_D1"
#define DEV_NAME "wiegand_reader"


typedef struct {
    uint16_t num;
    uint16_t fc;
} emmarine_t;


typedef struct
{
  union {
        unsigned long raw;
        emmarine_t card;
    };
  int bitcount;
} wiegand_buf_t;


static emmarine_t lastCard = {0, 0} ;
static wiegand_buf_t w_buf;

static struct timer_list w_timer;
static unsigned long wait_bit_timeout;

static struct timer_list last_card_timer;
static unsigned long wait_last_card_timeout;

static short int d0_irq = 0;
static short int d1_irq = 0;


//sysfs
static ssize_t sysfs_last_cardnum(
  struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
   unsigned long f = (lastCard.fc<<16)|lastCard.num;
   return sprintf(buf, "short:%.3d,%.5d long:%.10lu\n", lastCard.fc, lastCard.num, f);
}

static struct kobj_attribute wiegand_reader_attribute =
  __ATTR(cardnum, 0444, sysfs_last_cardnum, NULL);

static struct attribute *attrs[] =
{
  &wiegand_reader_attribute.attr,
  NULL,   /* need to NULL terminate the list of attributes */
};

static struct attribute_group attr_group =
{
  .attrs = attrs,
};

static struct kobject *wiegandKObj;


//irq handle
static irqreturn_t d0_isr(int irq, void *data)
{
    wiegand_buf_t *wb = (wiegand_buf_t *) data;
    int rc = 0;

    wb->raw <<= 1;
    wb->bitcount +=1;

    wait_bit_timeout =jiffies + msecs_to_jiffies(WIEGAND_BIT_TIMEOUT);
    rc= mod_timer( &w_timer, wait_bit_timeout );
    //if (rc==0) printk("Error in mod_timer d0 %d\n", rc);

    return IRQ_HANDLED;
}

static irqreturn_t d1_isr(int irq, void *data)
{
    wiegand_buf_t *wb = (wiegand_buf_t *) data;
    int rc = 0;

    wb->raw <<= 1;
    wb->raw = wb->raw |1;
    wb->bitcount +=1;

    wait_bit_timeout =jiffies + msecs_to_jiffies(WIEGAND_BIT_TIMEOUT);
    rc = mod_timer( &w_timer, wait_bit_timeout );
//        if (rc==0) printk("Error in mod_timer d1 %d\n", rc);
    return IRQ_HANDLED;

}

void wiegand_buf_clean(wiegand_buf_t *wb){
    wb->bitcount = 0;
    wb->raw = 0;
}

uint8_t calc_parity(uint16_t x)
{
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

uint8_t check_parity_w26(unsigned long nn){
    uint8_t  parity_calc, parity;

    parity = nn & 0x1;
    parity_calc = calc_parity((nn>>1) & 0xfff);   
    if (parity == (!parity_calc)){
        parity = (nn>>25) & 0x1;        
        parity_calc = calc_parity((nn>>13) & 0xfff);   
        if (parity == parity_calc) {
            return 1;        
        }
    }
    return 0;      
}


static void w_timer_callback(unsigned long data)
{
    wiegand_buf_t *wb = (wiegand_buf_t *) data;

    uint8_t correct;
    int rc;

    if (wb->bitcount==26){
        correct = check_parity_w26( wb->raw );
        if (correct){
            wb->raw >>= 1;
            wb->raw &=  0xffffff;
            lastCard = wb->card;
            sysfs_notify(wiegandKObj,NULL,"cardnum");
            wait_last_card_timeout =jiffies + msecs_to_jiffies(LAST_CARD_TIMEOUT);
            rc = mod_timer( &last_card_timer, wait_last_card_timeout );            
            printk("wiegand_reader: Read card -- short:%.3d,%.5d long:%.10lu\n", wb->card.fc, wb->card.num, wb->raw);
        } else {
            printk("wiegand_reader: Read parity error. buf=%lu, bitcount=%d\n", wb->raw, wb->bitcount);
        }
    } else {
        printk("wiegand_reader: Bitcount not equal 26. buf=%lu, bitcount=%d\n", wb->raw, wb->bitcount);
    }
    wiegand_buf_clean(wb);
}


static void last_card_timer_callback(unsigned long data)
{
    emmarine_t *lc = (emmarine_t *) data;
    lc->fc=0;
    lc->num=0;
}


int init_module(void)
{
    int rc;

    printk("wiegand_reader: Loading simple wiegand reader from gpio.\n");
    init_timer(&w_timer);
    w_timer.function = w_timer_callback;
    w_timer.data = (unsigned long) &w_buf;

    init_timer(&last_card_timer);
    last_card_timer.function = last_card_timer_callback;
    last_card_timer.data = (unsigned long) &lastCard;


    if((gpio_request(D0_GPIO, D0_GPIO_NAME) < 0)) {
        printk("wiegand_reader: Error request GPIO Pin D0.\n");
        return -1;
    }
    if(!(gpio_is_valid(D0_GPIO))) {
        printk("wiegand_reader: GPIO Pin D0 is invalid.\n");
        return -1;
    }
    d0_irq = gpio_to_irq(D0_GPIO);
    if( d0_irq < 0 ) {
        printk("wiegand_reader: GPIO Pin D0 irq not allowed.\n");
         return -1;
    }

    if( request_irq( d0_irq, d0_isr ,IRQF_TRIGGER_FALLING, D0_GPIO_NAME, &w_buf))
    {
        printk("wiegand_reader: Error request irq fo GPIO Pin D0.\n");
        return -1;
    }
    printk("wiegand_reader: GPIO PIN D0 configured.\n");


    if((gpio_request(D1_GPIO, D1_GPIO_NAME)) < 0) {
        printk("wiegand_reader: Error request GPIO Pin D1.\n");
        return -1;
    }
    if(!(gpio_is_valid(D1_GPIO))) {
        printk("wiegand_reader: GPIO Pin D1 is invalid.\n");
        return -1;
    }
    d1_irq = gpio_to_irq(D1_GPIO);
    if( d1_irq < 0 ) {
        printk("wiegand_reader: GPIO Pin D1 irq not allowed.\n");
         return -1;
         }

    if( request_irq( d1_irq, d1_isr ,IRQF_TRIGGER_FALLING, D1_GPIO_NAME, &w_buf))
    {
        printk("wiegand_reader: Error request irq fo GPIO Pin D1.\n");
        return -1;

    }
    printk("wiegand_reader: GPIO PIN D1 configured.\n");

    wiegand_buf_clean(&w_buf);

    //setup the sysfs
    wiegandKObj = kobject_create_and_add("wiegand_reader", kernel_kobj);

    if (!wiegandKObj)
    {
        printk("wiegand_reader: Failed to create sysfs\n");
        return -ENOMEM;
    }

    rc = sysfs_create_group(wiegandKObj, &attr_group);
    if (rc)
    {
        kobject_put(wiegandKObj);
    }

    lastCard.fc=0;
    lastCard.num=0;

    printk("wiegand_reader: Init completed.\n");
    
    return 0;
}


void cleanup_module(void)
{
    printk("wiegand_reader: Free resource.\n");
    del_timer(&w_timer);
    kobject_put(wiegandKObj);
    free_irq(d0_irq,&w_buf);
    gpio_free(D0_GPIO);
    free_irq(d1_irq,&w_buf);
    gpio_free(D1_GPIO);
    printk("wiegand_reader: Unload complete.\n");
}



