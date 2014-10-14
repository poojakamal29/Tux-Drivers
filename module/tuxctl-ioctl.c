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
	static char button_press;
	static int ack_flag = 0;
	unsigned int led_save; // save the state of the LED

	//hard code the hex for the seven segment display
	static unsigned char hex_led[16] =
	{ 0xE7, 0x06, 0xCB, 0x8F,
	  0x2E, 0xAD, 0xED, 0x86,
	  0xEF, 0xAF, 0xEE, 0x6D,
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
	int buttons_helper(struct tty_struct * , unsigned long );
	/* helper function to reinitialize and resture TUX on reset */
	void reset_helper(struct tty_struct *);
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
    		printk("event");
    		handle_bioc(b,c);
    		break;
    	case MTCP_ACK:
    		ack_flag = 0;
    		break;
    	case MTCP_RESET:
    		printk("reset");
    		reset_helper(struct tty_struct *);
    		break;
    	default:
    		break;
    }

    printk("packet : %x %x %x\n", a, b, c); 
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
		button_press = 0x00;
		tux_init(tty);
		return 0;
		break;
	case TUX_BUTTONS:
		if(arg == 0)
			return -EINVAL;
		return buttons_helper(tty, arg);
		break;
	case TUX_SET_LED:
		return set_LED(tty, arg);
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
	unsigned char buffer[2];
	spin_lock_init (&lock);

	button_press = 0x00;
	led_save = 0x000F0000;

	buffer[0] = MTCP_LED_USR;
	buffer[1] = MTCP_BIOC_ON;
	tuxctl_ldisc_put(tty, buffer, 2);

	clear_LED(tty);
	return;
}

/*
 * reset_helper
 * Description: restores interrupt enabling and led setting along with previous led values
 * after MTCP_RESET interrupt occurs.
 * Inputs: tty - pointer to a tty_struct
 * Outputs: None
 * Returns: None
 * Side Effects: Restores leds and button usage on the TUX controller
 */
void reset_helper(struct tty_struct* tty)
{
	char reset_buffer_array[2];

	/* reinitialize the TUX so it's usable */
	reset_buffer_array[0] = MTCP_LED_USR;
	reset_buffer_array[1] = MTCP_BIOC_ON;

	tuxctl_ldisc_put(tty, reset_helper, 2);

	clear_LED(tty);
	set_LED(tty, led_save);
}

/*
 * handle_bioc_event(unsigned char b, unsigned char c)
 * Description: When the interrupt MTCP_BIOC_EVENT comes in from the TUX, 
 * this function parses the data into a usable format to return to user.
 * Inputs: b - byte 1 from packet that's received
 *		   c - byte 2 from packet that's received
 * Outputs: None
 * Returns:	None
 * Side Effects: populates button buffer to be populated into user space
 */

void handle_bioc_event (unsigned char b, unsigned char c)
{
	unsigned char bit_mask = 0x0F;
	unsigned char mask_two = 0xFF;
	spin_lock(&lock);

	b &= bit_mask;	//mask high 4 bits
	c &= bit_mask; // mask low 4 bits
	c = c << 4;    //shift packet c to MSB

	button_press = b | c; //combine b and c packets into 1
	button_press &= mask_two;
	spin_unlock(&lock);
}

/*
 * buttons_helper(struct tty_struct*, unsigned long arg)
 * Description: Copies the correctly parsed button arg into userspace.
 * Inputs:	tty - pointer to a tty_struct
 * 			arg - pointer to an integer in user space
 * Outputs: None
 * Returns:	Returns 0 upon successful copy, -EINVAL on unsuccessful copy
 * Side Effects: Writes into user space and ideally controls movement of player
 */

 int buttons_helper(struct tty_struct*, unsigned long arg)
 {
 	/* variable declartions */
 	unsigned int num_seconds = (unsigned int)arg;
	unsigned int data = num_seconds;
	unsigned char 3rd_digit = 2nd_digit = 1st_digit = 0th_digit = 0x0;
	unsigned char seven_segment[4];
	unsigned int leds = num_seconds >> 16;
	unsigned char led_array[4];
	unsigned int dec = num_seconds >> 24;
	unsigned char 3rd_dec = 2nd_dec = 1st_dec = 0th_dec = 0x0;
	unsigned char led_buf[6];
	int i;
	int index = 2;

 	if(num_seconds != saved_led)
 	{
 		led_save = num_seconds;

 		clear_LED(tty);

 		data &= 0x0000FFFF;

 		0th_digit = data & 0x000F;
 		1st_digit = (data & 0x00F0) >> 4;
 		2nd_digit = (data & 0x0F00) >> 8;
 		3rd_digit = data >> 12;

 		seven_segment[0] = hex_led[0th_digit];
 		seven_segment[1] = hex_led[1st_digit];
 		seven_segment[2] = hex_led[2nd_digit];
 		seven_segment[3] = hex_led[3rd_digit];

 		leds &= 0x000F;

 		led_array[0] = leds & 0x1;
 		led_array[1] = (leds & 0x2) >> 1;
 		led_array[2] = (leds & 0x4) >> 2;
 		led_array[3] = leds >> 3;

 		seven_segment[0] |= 0th_digit << 4;
 		seven_segment[1] |= 1st_digit << 4;
 		seven_segment[2] |= 2nd_digit << 4;
 		seven_segment[3] |= 3rd_digit << 4;

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
 	}
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
	clear_buf[1] = 0xFF;
	clear_buf[2] = 0x00;
	clear_buf[3] = 0x00;
	clear_buf[4] = 0x00;
	clear_buf[5] = 0x00;

	tuxctl_ldisc_put(tty, clear_buf, 6);
}