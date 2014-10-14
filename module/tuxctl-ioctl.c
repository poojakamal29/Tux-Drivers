/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

	/* global variables */
	static unsigned char button_press;
	static int ack_flag = 1;
	static unsigned int led_save; // save the state of the LED
	static int reset_flag = 0;

	//hard code the hex for the seven segment display
	static unsigned char hex_led[16] =
	{ 0xE7, 0x06, 0xCB, 0x8F,
	  0x2E, 0xAD, 0xED, 0x86,
	  0xEF, 0xAE, 0xEE, 0x6D,
	  0xE1, 0x4F, 0xE9, 0xE8
	};

	/* declare some locks */
	spinlock_t lock;


	/* Helper Function Declartions */

	/* Initial tux controller */
	void tux_init(struct tty_struct *);
	/* handle the button interrupt */
	void handle_bioc(unsigned char, unsigned char );
	/* copies parsed button value into user space */
	int buttons(struct tty_struct * , unsigned long );
	/* helper function to reinitialize and resture TUX on reset */
	void reset(struct tty_struct *);
	/* helper function to set LED in the TUX */
	int set_LED(struct tty_struct* tty, unsigned long arg);
	/* helper function that clears the leds */
	void clear_LED(struct tty_struct* tty);


/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

    switch(a)
    {
    	case MTCP_BIOC_EVENT:
    		handle_bioc(b,c);
    		break;
    	case MTCP_ACK:
    		if(reset_flag == 1)
    		{
    			reset_flag = 0;
    			set_LED(tty, led_save);
    		}
    		break;
    	case MTCP_RESET:
    		reset(tty);
    		break;
    	default:
    		break;
    }

    //printk("packet : %x %x %x\n", a, b, c); 
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		tux_init(tty);
		return 0;
		break;
	case TUX_BUTTONS:
		if(arg == 0)
			return -EINVAL;
		return buttons(tty, arg);
		break;
	case TUX_SET_LED:
		set_LED(tty, arg);
		return 0;
		break;
	default:
	    return -EINVAL;
	    break;
    }
}

/*
 * tux_init(struct tty_struct* tty)
 * Description: Helper function used to initialize all ports, controllers, and etc. into the correct modes and usages
 * Inputs: tty - pointer to a tty_struct for use in calling the function tuxctl_ldisc_put
 * Outputs: None
 * Returns: None
 * Side Effects: Opens the port to write into the TUX, sets TUX into LED User mode, turns BIOC on
 */

void
tux_init (struct tty_struct * tty)
{
	unsigned char buf[2];
	spin_lock_init (&lock);

	button_press = 0x00;
	led_save = 0x000F0000;

	buf[0] = MTCP_LED_USR;
	buf[1] = MTCP_BIOC_ON;
	tuxctl_ldisc_put(tty, buf, 2);

	clear_LED(tty);
	return;
}

/*
 * reset
 * Description: restores interrupt enabling and led setting along with previous led values
 * after MTCP_RESET interrupt occurs.
 * Inputs: tty - pointer to a tty_struct
 * Outputs: None
 * Returns: None
 * Side Effects: Restores leds and button usage on the TUX controller
 */
void reset(struct tty_struct* tty)
{
	char reset_buffer_array[2];
	spin_lock(&lock);
	/* reinitialize the TUX so it's usable */
	reset_buffer_array[0] = MTCP_LED_USR;
	reset_buffer_array[1] = MTCP_BIOC_ON;

	reset_flag = 1;
	tuxctl_ldisc_put(tty, reset_buffer_array, 2);
	//clear_LED(tty);

	/* if led is not in a cleared state, set it */
/*	if(led_save != 0x000F0000)
	{
		set_LED(tty, led_save);
	}*/
	spin_unlock(&lock);
}

/*
 * handle_bioc(unsigned char b, unsigned char c)
 * Description: When the interrupt MTCP_BIOC_EVENT comes in from the TUX, 
 * this function parses the data into a usable format to return to user.
 * Inputs: b - byte 1 from packet that's received
 *		   c - byte 2 from packet that's received
 * Outputs: None
 * Returns:	None
 * Side Effects: populates button buffer to be populated into user space
 */

void handle_bioc (unsigned char b, unsigned char c)
{
	unsigned char bit_mask = 0x0F;
	spin_lock(&lock);

	b &= bit_mask;	//mask high 4 bits
	c &= bit_mask; // mask low 4 bits
	c = c << 4;    //shift packet c to MSB

	button_press = b | c; //combine b and c packets into 1
	spin_unlock(&lock);
}

/*
 * buttons(struct tty_struct*, unsigned long arg)
 * Description: Copies the correctly parsed button arg into userspace.
 * Inputs:	tty - pointer to a tty_struct
 * 			arg - pointer to an integer in user space
 * Outputs: None
 * Returns:	Returns 0 upon successful copy, -EINVAL on unsuccessful copy
 * Side Effects: Writes into user space and ideally controls movement of player
 */

int buttons(struct tty_struct* tty, unsigned long arg)
{
	
	int* user = (int*)arg;
	int valid;
	spin_lock(&lock);
	valid = copy_to_user(user, &button_press, 1);
	spin_unlock(&lock);
	if (valid != 0)
		return -EINVAL;
	return 0;	
}

/*
 * set_LED(struct tty_struct* tty, unsigned long arg)
 * Description: Copies to user space the correctly parsed button argument.
 * Inputs:	tty - pointer to a tty_struct
 * 			arg - pointer to an integer in user space
 * Outputs: None
 * Returns:	int
 * Side Effects: Set's the LED's to the values that need to be displayed. Ideally, the time.
 */
 int set_LED(struct tty_struct* tty, unsigned long arg)
 {
 	/* variable declartions */
 	unsigned int num_seconds = (unsigned int)arg;
	unsigned int data = num_seconds;
	unsigned int leds = num_seconds >> 16;
	unsigned int dec = num_seconds >> 24;
	// fourth digit is left most digit
	unsigned char digits[4];
	unsigned char seven_segment[4];
	unsigned char led_array[4];
	unsigned char decimals[4];
	unsigned char led_buf[6];
	int i;
	int index = 2;
		/* lock while setting the global variable led_save */
		spin_lock(&lock);
 		led_save = num_seconds;
 		spin_unlock(&lock);

 		clear_LED(tty);

 		data &= 0x0000FFFF;

 		/* look at each byte by using masks and shifting*/
 		digits[0] = data & 0x000F;
 		digits[1] = (data & 0x00F0) >> 4;
 		digits[2] = (data & 0x0F00) >> 8;
 		digits[3] = data >> 12;
 		/* fill the seven segment display with the appropriate hex */
 		seven_segment[0] = hex_led[digits[0]];
 		seven_segment[1] = hex_led[digits[1]];
 		seven_segment[2] = hex_led[digits[2]];
 		seven_segment[3] = hex_led[digits[3]];
 		/* look at the first byte in leds */
 		leds &= 0x000F;
 		/* fill the led_array with the appropriate bit */
 		led_array[0] = leds & 0x1;
 		led_array[1] = (leds & 0x2) >> 1;
 		led_array[2] = (leds & 0x4) >> 2;
 		led_array[3] = leds >> 3;
 		/* mask the decimal point */
 		dec &= 0x0F;
 		/* look at each bit of the decimals */
 		decimals[0] = dec & 0x1;
 		decimals[1] = (dec & 0x2) >> 1;
 		decimals[2] = (dec & 0x4) >> 2;
 		decimals[3] = dec >> 3;

 		/*shift the high bit to make it the MSB */
 		seven_segment[0] |= (decimals[0] << 4);
 		seven_segment[1] |= (decimals[1] << 4);
 		seven_segment[2] |= (decimals[2] << 4);
 		seven_segment[3] |= (decimals[3] << 4);

 		led_buf[0] = MTCP_LED_SET;
 		led_buf[1] = leds;

 		for(i = 0; i < 4; i++)
 		{
 			if (led_array[i] == 1)
 			{
 				led_buf[index] = seven_segment[i];
 				index++;
 			}
 		}
 		tuxctl_ldisc_put(tty, led_buf, index);
 	return 0;
 }

 /*
 * clear_LED(struct tty_struct*)
 * Description: this helper clears the led by sending a buffer with all values back to
 * when they were first initialized
 * Inputs:	tty - pointer to a tty_struct
 * Outputs: None
 * Returns: None
 * Side Effects: clears the led
 */
void clear_LED(struct tty_struct* tty) {
	unsigned char clear_buf[6];

	clear_buf[0] = MTCP_LED_SET;
	clear_buf[1] = 0x0F;
	clear_buf[2] = 0x00;
	clear_buf[3] = 0x00;
	clear_buf[4] = 0x00;
	clear_buf[5] = 0x00;

	tuxctl_ldisc_put(tty, clear_buf, 6);
}

