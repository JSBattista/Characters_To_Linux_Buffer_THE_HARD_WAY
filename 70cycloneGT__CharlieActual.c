/*
 * 70cycloneGT__CharlieActual
 *
 * Main Charlie Project driver program for Cyclone GT. 
 * 	Main display
 * 	Sensor data display
 * 	Warning display
 * 
 * compile with 'gcc -O2 -o 70cycloneGT__CharlieActual 70cycloneGT__CharlieActual.c'
 * run with './70cycloneGT__CharlieActual'
 *
 *
 special thanks  https://forum.43oh.com/topic/10366-random-beaglebone-code-snippets/
	 https://stackoverflow.com/questions/11564336/array-of-pointers-to-structures
 *
 */
// Basic Includes
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/ioctl.h>
#include <time.h>
#include <errno.h>

// GPIO related includes-----------------------------------------------
#define GPIO_BASE__P8   0x44e10800
#define GPIO_BASE__P9   0x44e10000
#define GPIO_BASE       0x44e10800
#define MMAP_OFFSET     0x44C00000
#define BASEADDR_GPIO0 0x44E07000
#define BASEADDR_GPIO1 0x4804C000
#define BASEADDR_GPIO2 0x481AC000
#define BASEADDR_GPIO3 0x481AE000  
#define GPIO_SIZE  0x00000FFF      // decimal 4,095‬  This is the size of each register ie start 0x4804_C000 end 0x4804_CFFF 4KB
// OE: 0 is output, 1 is input
#define GPIO_OE 0x14d
#define GPIO_IN 0x14e
#define GPIO_OUT 0x14f
#define PYRO_1_IN (1<<28)   // GPIO 1_28
#define USR__OUT (1<<13)   // GPIO 1_13
short clearflag = 0;		// global variables to govern screen clearance.	
unsigned char priorityflag = 0;
unsigned char selectflag = 0;


// ADC global
// With different runs of the program, the actual handle number changes to the ADC ports, so it's best
// to rely on a named variable and not an array and assume something like "element zero is ADC zero". 

struct adc__container {
	char adc_read[5]; // = {0};  // input buffer
	unsigned char blen;// = sizeof(adc_global.adc_read - 1);  // deb100 review this as a potential failure point.
	const char* ain[6];  // paths
    	unsigned char len;  // length of data from call
};
struct adc__container adc_global = {{0}, 
				    sizeof(adc_global.adc_read -1), 
				    {
					"/sys/bus/iio/devices/iio:device0/in_voltage0_raw",		      
				     	"/sys/bus/iio/devices/iio:device0/in_voltage1_raw",
				     	"/sys/bus/iio/devices/iio:device0/in_voltage2_raw",
		      		     	"/sys/bus/iio/devices/iio:device0/in_voltage3_raw",
	    	      		     	"/sys/bus/iio/devices/iio:device0/in_voltage4_raw",
		      		     	"/sys/bus/iio/devices/iio:device0/in_voltage5_raw"
				    },
      				    0};
// GPIO global
char *gpio_mem, *gpio_map;
// I/O access
volatile unsigned *gpio;
static void io_setup(void)
{


	// Analog setup  -------------------------------------------------
	//short adc_in[6];
	//adc = 0;
	const char* t_ain[6] ={"/sys/bus/iio/devices/iio:device0/in_voltage0_raw",
			      "/sys/bus/iio/devices/iio:device0/in_voltage1_raw",
			      "/sys/bus/iio/devices/iio:device0/in_voltage2_raw",
			      "/sys/bus/iio/devices/iio:device0/in_voltage3_raw",
		    	      "/sys/bus/iio/devices/iio:device0/in_voltage4_raw",
			      "/sys/bus/iio/devices/iio:device0/in_voltage5_raw"};
	for (unsigned char ucc = 0; ucc < 6; ucc++) {
		adc_global.ain[ucc] = t_ain[ucc];  // So nth handle is nth ADC port
	//	adc_global.adc_handle[ucc] = open(adc_global.ain[ucc], O_RDONLY);
	//	if(adc_global.adc_handle[ucc] == -1){
	//		perror(adc_global.ain[ucc]);
	//		exit(1);
	//	}   // if
	}  // for
	adc_global.blen = sizeof(adc_global.adc_read - 1);  // deb100 review this as a potential failure point.



	// Digital setup  -------------------------------------------------
    	int mem_fd;
	    /* open /dev/mem */
   	 if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
		 printf("can't open /dev/mem \n");
	         exit (-1);
   	 }  // if
   	 /* mmap GPIO */
   	 gpio_map = (char *)mmap(
      				 0,
			         GPIO_SIZE,
        			 PROT_READ|PROT_WRITE,
        			 MAP_SHARED,
        			 mem_fd,
        			BASEADDR_GPIO1     //  don't drop the base - needs to change depending on what GPIO pin is in use
   	 );
   	 close(mem_fd);  // note needed any more
  	 if (gpio_map == MAP_FAILED) {
           	 printf("mmap error %d\n", (int)gpio_map);
           	 exit (-1);
   	 }
   	 // Always use the volatile pointer!
   	 gpio = (volatile unsigned *)gpio_map;
   	 *(gpio + GPIO_OE) |= (PYRO_1_IN);  // Register specific input pin in the bank. Note: input may still work without this command. 
	//  FOR OUTPUT DO NOT DELETE   *(gpio + GPIO_OE) &= ~(USR__OUT); // Register output. Entire bank can be "set" this way, but that ruins input. 
}

unsigned short read__ADC(unsigned short adcp) {   
	short th =  open(adc_global.ain[adcp], O_RDONLY);
	if(th == -1){
		perror(adc_global.ain[adcp]);
		return 0; //exit(1);   // deb review error handling and needed error flag value
	}   // if
	adc_global.len = read(th, adc_global.adc_read, adc_global.blen);
	close(th);
	if(adc_global.len)return strtol(adc_global.adc_read, NULL, 10);
	return 0;
}

// Graphics and text generation.
// These are the sizes of the individual character arrays
#define CHAR_ARR__29x24 696
#define CHAR_ARR__10x14 168
unsigned char *ascii_characters_BIG[128];	// Store the ASCII character set, but can have some elements blank
unsigned char *ascii_characters_SMALL[128];	// Store the ASCII character set, but can have some eleunsigned char *c2[128];
unsigned char *numbers_BIG[10];		// For quicker number display routines, these arrays of pointers to the numbers
unsigned char *numbers_small[10];
// 'global' variables to store screen info and take the frame buffer.
int fbfd = 0;
char *fbp = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
int page_size = 0;
int cur_page = 0;
unsigned short base_color = 77;
unsigned short alert_color = 223;
unsigned short critical_color = 714;
time_t timer_global; // = time(NULL);









// This is the heart of most of the drawing routines except where memory copy or move is used. 
void put_pixel(unsigned short x, unsigned short y, unsigned short c)
{
    // calculate the pixel's byte offset inside the buffer
    unsigned int pix_offset = x + y * finfo.line_length;
    // offset by the current buffer start
    pix_offset += cur_page * page_size;
    // now this is about the same as 'fbp[pix_offset] = value'
    *((char*)(fbp + pix_offset)) = c;
}
// helper function to draw a rectangle in given color
void fill_rect(unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c) {
    int cx, cy;
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
            put_pixel(x + cx, y + cy, c);
        } // for
    }
}
// Special thanks to:
// https://www.thecrazyprogrammer.com/2017/01/bresenhams-line-drawing-algorithm-c-c.html
// Algorithm originally flawed as it could not handle a vertical line
void drawline(unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1, unsigned short c) {
    	int dx, dy, p, x, y;
	dx=x1-x0;
	dy=y1-y0;
	x=x0;
	y=y0;
	p=2*dy-dx;
	while(x<=x1) {
		if (x == x1) { // Handle vertical line, which this algorithm originally does not do 
			put_pixel(x,y++,c);
			if (y > y1) break;
		} else {
			if(p>=0){ 
				put_pixel(x,y,c);
				y=y+1;
				p=p+2*dy-2*dx;
			} else {
				put_pixel(x,y,c);
				p=p+2*dy;
			} // if
			x=x+1;
		} // if
	}  // while
}
// This verson creates rectangles with border widths.
void draw_rect(unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c, unsigned short b) {
    if (b == 1) {
	drawline(x, y, x + w, y, c);
	drawline(x, y + h, x + w, y + h, c);
	drawline(x + w, y, x + w, y + h, c);
	//drawline(x, y, x, y + h, c);
	x++;
	drawline(x, y, x, y + h, c);  // might be an issue with buffer
	return;
    }
    unsigned short cx, cy;
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
			if (((cx < b) || ((w-cx) <= b))  || 
			    ((cy < b) || ((h-cy) <= b))){
			       	put_pixel(x + cx, y + cy, c);
			} // if
	} // for
    } // for
}





// This function assigns color directly to the elements of the glyph
// array for faster use with memcpy later instead of 1's and 0s checking if desired.
void setColor(unsigned char* a, unsigned short  h, unsigned short w, unsigned short c) {
	unsigned int cx, cy;
	for (cy = 0; cy < h; cy++) {
		for (cx = 0; cx < w; cx++) {
			if (a[cy*w + cx] > 0) { // if the array has a 1 in this element... 
				a[cy*w + cx] = c;
			} else {
				a[cy*w + cx] = 0;
			} // if
		} // for
	} // for
}
// Draw an individual character by the array with a color value
void draw_char(char *glyph, unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c) {
    unsigned short cx, cy;
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
			if (glyph[cy*w + cx] > 0){ // if the array has a 1 in this element... 
				put_pixel(x+cx, y+cy, c);
			} else  {
				put_pixel(x+cx, y+cy, 0);
			} // if
        } // for
    } // for
}
// draw an individual character by the array with a color value AND a background color value for "highlighted" text
// this is essentially just using a background color value and an "else" branch for the 0s. Good for highlighing. 
void draw_charBG(char *glyph, unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c, unsigned short cb) {
    unsigned short cx, cy;
    for (cy = 0; cy < h; cy++) {
        for (cx = 0; cx < w; cx++) {
			if (glyph[cy*w + cx] > 0){ // if the array has a 1 in this element... 
				put_pixel(x+cx, y+cy, c);
			} else {
				put_pixel(x+cx, y+cy, cb);
			}// if
        } // for
    } // for
}
// This version assumes that the color value is already assigned into the element of the array.
// Thus a color value is not given, because it's already there. 
// This routine is NOT calling the put_pixel routine. Whatever is in the "line" goes direct.
// Note: the placement assumes that the X,Y values are not going to be less than 1,1, thus
// the most upper left of the screen is 1,1, and positive Y is "downward". Do not call this 
// routine with X.Y = 0,0
// Note: on the test system this method had strange side effects and there was  no noticable
// performance improvement. Much may depend on how the system implements memcpy.  
void draw_charAR(char *glyph, unsigned short x, unsigned short y, unsigned short w, unsigned short h) {
    for (unsigned short cy = 0; cy < h; cy++) {
		memcpy(&fbp[((y-1 + cy) * vinfo.xres) + x], &glyph[cy*w], w *  sizeof(char));		
    } // for
}
// This call to draw string takes X and Y location and color, and spacing between the characters=
// Assumably a null char* array is never handed to this function
// Due to the nature of these strings a length is needed
// x = location x0
// y = location y
// s is the string
// l is the length of the string
// c is the color
// cb is background color
// sp is the spacing between characters
// sz is the size of the character. Use this variable to add sizes later if needed
// spacing is the distance between the characters
//  Generally everything is known "up front" before the function call so there is no checking in here except for null pointers in the character array
// in case it's forgotten that the particular ASCII representation was not appointed with an array comprising a character. 
// Depending on which values are given to the variables, this function may call on several versions of character draw routines. 
void draw_string(unsigned short x, unsigned short y, char *s, unsigned short l, unsigned short c, unsigned short cb, unsigned short sp, unsigned char sz){
	//printf("deb77a x = %i, y = %i, data  = %s\n", x, y, s);
	unsigned short incr = 0;
	unsigned short lh, lw;
	x = x - sp; // the loop will add it back for the first character.
	unsigned char **ascii_characters_local;  // handle to the array that is going to be used based on the sz variable
	switch (sz) {  
		case 1:  ascii_characters_local = ascii_characters_SMALL; 
				 lh = 14;
				 lw = 10;
				 break;
		case 2:  ascii_characters_local = ascii_characters_BIG; 
				 lh = 29;
				 lw = 24;
		         break;
		default: ascii_characters_local = ascii_characters_SMALL; 
				 lh = 14;
				 lw = 10;
				 break;  // redundant
	} // switch
	for (unsigned short incr = 0; incr < l; incr++) {  // loop length of "string" array of characters.
		x+=sp;
		if((unsigned short)s[incr] == 32) {
			x += lw;
			continue;
		}
		if (c > 0){ // Color is given to check if only a fore color was given, or a fore and back
			(cb > 0) ? draw_charBG(ascii_characters_local[(unsigned short)s[incr]], x+(incr*lw), y, lw, lh, c, cb): // Background color
			           draw_char(ascii_characters_local[(unsigned short)s[incr]], x+(incr*lw) , y, lw, lh, c);  // No background color
		} else {  // Color is in the glyph array itself
			draw_charAR(ascii_characters_local[(unsigned short)s[incr]], x+(incr*lw), y, lw, lh);	// Array "blit" with mem copy
		} // if
	} // for
}
// this is a quick decimal display function that calls on draw_char. Generally, decimals are characters, but this calls from the 
// secondary pointer array that points to characters representing numbers wereby the index is the proper number. 
// this function is recursive
void decdisp(unsigned char **aloc, unsigned short x, unsigned short *y, unsigned short *h, unsigned short *w, unsigned short *offs, unsigned short *c, unsigned short *bg,  unsigned int divider, unsigned int value) {
	if (divider == 1) return;
	if ((value % divider) == value) {
		(bg > 0) ? draw_charBG(aloc[value/(divider/10)], x, *y, *w, *h, *c, *bg): // Background color
		           draw_char(aloc[value/(divider/10)], x, *y, *w, *h, *c);  // No background color
		x += *w;
		x += *offs;
		decdisp(aloc, x, y, h, w, offs, c, bg, (divider / 10), value % (divider / 10));
	} // if
}
// this function calls the recursive decdisp function for the display of numbers in a more easier format where 
// the numbers are delivered as actual numbers and not characters. 
void draw_numbers(unsigned short x, unsigned short y, unsigned short offs, unsigned short c, unsigned short cb,  unsigned short sz, unsigned int divider, unsigned int value) {
    	unsigned char **ascii_characters_local;  // handle to the array that is going to be used based on the sz variable
	unsigned short lh, lw;
	switch (sz) {   // height and width are established based on that size variable, almost like a "font size"
		case 1:  ascii_characters_local = numbers_small; 
			 lh = 14;
			 lw = 10;
			 break;
		case 2:  ascii_characters_local = numbers_BIG; 
			 lh = 29;
			 lw = 24;
	         	 break;
		default: ascii_characters_local = numbers_small; 
			 lh = 14;
			 lw = 10;
			 break;  // redundant
	} // switch
	unsigned short *yval, *heightval, *widthval, *offsetval, *colorval, *bgcolor; // pointers for the recursive call
	yval = &y;
	heightval = &lh; 
	widthval = &lw;
	offsetval = &offs;
	colorval = &c;
	bgcolor = &cb;
	decdisp(ascii_characters_local, x, yval,  heightval, widthval, offsetval, colorval, bgcolor, divider, value);	
}
// helper to clear (fill with given color) the screen
void clear_screen(int c) {
    memset(fbp + cur_page * page_size, c, page_size);
}
// clear a section defined by X, Y, L, and H values similar to a rectangle. Good for saving work instead of clearing an entire screen. 
void clear_ROI(unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c) {
	fill_rect(x, y, w, h, c);
}
// Common display area clear
// deb004
void clear_ROI_primary() {
	clear_ROI(2, 
		  200, 
		  2200, 
  		  400, 
		  0);  
}
void setclear_priority_LOCK(){
	printf("LOCK CALLED DEB23423423\n");
	if(priorityflag == 0){
		priorityflag = 1;
		clearflag = 1;  // Trigger cleanup	
	} // if
}
void setclear_priority_UNLOCK(){
	printf("UNLOCK CALLED DEB23423423\n");
	if (priorityflag) {
		priorityflag = 0;
		clearflag = 1;	// Trigger cleanup
	}  // if
    	draw_rect(finfo.line_length - 600, 600, 600, 100, base_color, 5);
	draw_string(finfo.line_length - 500, 630, (char*)"STATUS NOM", 10, base_color, 0, 10, 2);
}




//-----------------------------------------------------------------------------------------------
// put all Charlie-specific code here for easier debugging.
// deb772 notes as follows
// "blind" struct elements leave too much to chance when there is total code control making it redundant
// So define here what the sensor set for the specific systems are by virtue of what each unique structure created for it
// has in it, and what function it points to. 


// this is a structure that hold information for I2C communications pertinent to any system using it
struct I2CCOMS {
	//deb100 need to have the variables for addresses, etc, here 
};

// this drives displays of status cubes for main status screen which is basically static
// Note: to make screen updates possible with localized clearing and save time, keep everything INSIDE of rectangle.
struct template__SYSSTATCUBE {
	char* title;
	unsigned short title_length;
	char* status;
	unsigned short status_length;
	unsigned short locX, locY;	// Where actually this goes on-screen
	unsigned short title_offsetX;
	unsigned short title_offsetY;
	unsigned short status_offsetX;
	unsigned short status_offsetY;
	unsigned short width, height, border; 
	unsigned short color;
	unsigned short buttonref;
};

// basic "hold the X, Y, etc" stuff for indications so to avoid laboriously long function calls and make arguments neater
// So these would be points specific to an indication
// Note the scale and ratio. For example if a scale is 50 with a ratio of 1 and 1, then the entire gauge is going to be in a square of 50 pixels by 50 pixels
// If the scale is say 100 and the ratio is 1 by 2, then the gauge is going to be 100 accross by 200 high. 
struct template__gauge {
	unsigned short loc__X;
	unsigned short loc__Y;
	char* maintitle;     // Title of the scale that "floats" next to it
	unsigned short titlefont;
	unsigned short titlelength;
	unsigned short titlespacing;
        unsigned short titleoffset__X; // This is the title offset from the XY coordinates placement of the indication totally
	unsigned short titleoffset__Y; 	
	unsigned short min;  // Minimum scale value of the measured quantity
	unsigned short max;  // Maxium value of the scale of the measured quantity
	unsigned short warnlevel;  // level at which things turn yellow
	unsigned short warnrange;  // range up and down from the respective level
	unsigned short warncolor; // It may not always be yellow 
	unsigned short criticallevel; // level at which things turn red
	unsigned short criticalrange;  // range up and down from the respective level
	unsigned short criticalcolor; //  critical color can be anything depending on that which is critical
	unsigned short greenzone; // This is the area where "green" is established. Thus warn/critical levels can be <>  then levels around this value
	unsigned short greenrange;  // range up and down from the respective level
	unsigned short greencolor; // It may not always be green 
	unsigned short scale; // Here is the ultimate determination of size from the origin XY point
	unsigned short ratioXY[2]; // A X by Y scaling, like 4:3 for example
	unsigned short divider; // this is the number range, like a three digit number has a diver of 100
	// Note: So a 4:3 ratio with a scale of 50 means an X length of 50 * 4 and a Y height of 50 * 3
	// Note: function pointer should go here for FILL rect or DRAW Rect calls which may vary
	// Note: function pointer for border if needed  - that is, if not null, then there's a border according to X, Y size, and scale
};
// The bargraph gets much of its data from the gauge template, but the function uses a spacing/widening routine in the graphics
struct template__bargraph {
	unsigned short loc__Ys[13];
 	unsigned short spacing__Ys[13];	
};


// Information-specific data structures as it would pertain to a car, perhaps
// NOTE: Some routines might be passing these as "void" pointers to functions so always make sure priority is the first 
struct sensors__TRANS  {
	unsigned short priority;  // control of display hierarchy
	unsigned char select;	  // force pririty 0 response to force indication display focus
	time_t current;
	char current_gear;   
	unsigned short trans_temp;
	unsigned short trans_press;
	struct template__SYSSTATCUBE disp__mainstat;
	struct template__gauge indicator;  //  limits are in here.
};
struct sensors__FUEL { // container for fuel status at present
	unsigned short priority;
	unsigned char select;	  // force pririty 0 response to force indication display focus
	time_t current;
	unsigned short gallons;
	unsigned short max_gallons;
	unsigned short max_volts;	// This is a voltage drop going to to fuel gauge at max capacity
	unsigned short min_volts; 	// At minimum capacity, this would be the volts in the system.
	unsigned short pressure;  // Not commonly used except in racing
	char status;     // E, B, F - B is for Bingo
	struct template__SYSSTATCUBE disp__mainstat;
	struct template__gauge indicator;   // the stats are also important here for gauge coloration
};
struct sensors__COOLANT {
	unsigned short priority;
	unsigned char select;	  // force pririty 0 response to force indication display focus
	time_t current;
	unsigned short temp__coolant_upstream; 	// Temperature that is "upstream" or on the hot side of the radiator
	unsigned short temp__coolant_downstream; // This is the "cool side" of the radiator
	unsigned short max_nominal;  // actual temps
	unsigned short min_nominal;
	// note engine coolant at the block may be redundant considering where the sensor is, which is "upstream" anwyay, maybe leave alone.
	unsigned short temp__coolant_engine; // This would be the overall engine coolant temperature at the block where the sensor is.
	struct template__SYSSTATCUBE disp__mainstat;
	struct template__gauge indicator;  // 12-28-2019 not yet in use....
};
struct detectors__FIRE {
	unsigned short priority;
	unsigned char select;	  // force pririty 0 response to force indication display focus
	time_t current;
	unsigned short quadrant; // This is the quandrant the fire is located in, per the sensor
	char* status;
	struct template__SYSSTATCUBE disp__mainstat;
}; 
struct sensors__OIL{
	unsigned short priority;
	unsigned char select;	  // force pririty 0 response to force indication display focus
	time_t current;
	unsigned short pressure;
	unsigned short temperature; 
	struct template__SYSSTATCUBE disp__mainstat;
	struct template__gauge indicator;  // Put limits in here. 
};
// Here would be an O2 probe system.
struct probes__O2 {
	unsigned short priority;
	unsigned char select;	  // force pririty 0 response to force indication display focus
	unsigned short probestats[8];   // array to store incoming data from the ADC system 
	unsigned short lowADC;
	unsigned short highADC;
	time_t current;
	struct template__SYSSTATCUBE disp__mainstat[8];
	struct template__gauge indicators[8];  //  right here, set up for 8 cylinders
	struct template__bargraph* bgtemplate_O2;  // one can be used for all 
	unsigned short displayall;
};
// deb773 special notes:
// The handling of system specific checks are done by special subroutines that have 2 parts each: a first half of each sub
// is going to be just a status check and controlling the status display cube. From there user input determines gauge/indication display, 
// with the exception being fire. 
// The handling subroutines will rely on arguments if they are to go further than the display cube and show value indication with gauges. 
// these special functions may garner priroty values or maybe in the structure so that if the structure of the indication system has a lesser 
// prirotiy or one system is top prirotiy, then that is the only one shown - Fire would be such an indication system or section. 


// auxiliary functions
//
void display_bar__vertical( struct template__gauge* gstat, unsigned short disval){
	// Here goes goes for a vertical gauge display
	// here are the "extents" note: Some of this might be put in gstat build code instead of being constantly redone here.
	unsigned short scaledX = gstat->ratioXY[0] * gstat->scale;
	unsigned short scaledY = gstat->ratioXY[1] * gstat->scale;
	unsigned short currentcolor;
	if((disval >= gstat->criticallevel - gstat->criticalrange) &&  (disval <= gstat->criticallevel + gstat->criticalrange)) {  
		currentcolor = gstat->criticalcolor; 
	} else if ((disval >= gstat->warnlevel - gstat->warnrange) && (disval <= gstat->warnlevel + gstat->warnrange)){
		currentcolor = gstat->warncolor; 
	} else {
		currentcolor = gstat->greencolor; // Generally assume it's OK if warn and critical are not met
	}  // if
	draw_string(gstat->loc__X + gstat->titleoffset__X, gstat->loc__Y + gstat->titleoffset__Y, (char*)gstat->maintitle, gstat->titlelength, currentcolor, 0, gstat->titlespacing, gstat->titlefont);
	drawline(gstat->loc__X, gstat->loc__Y + scaledY +1, gstat->loc__X + scaledX, gstat->loc__Y + scaledY +1, currentcolor); // Short line on the bottom
	drawline(gstat->loc__X, gstat->loc__Y, gstat->loc__X, gstat->loc__Y + scaledY, currentcolor); // Left side line
	drawline(gstat->loc__X+1, gstat->loc__Y, gstat->loc__X+1, gstat->loc__Y + scaledY, currentcolor); // Left side line
	//drawline(gstat->loc__X+2, gstat->loc__Y, gstat->loc__X+2, gstat->loc__Y + scaledY, currentcolor); // Left side line
	//drawline(gstat->loc__X+3, gstat->loc__Y, gstat->loc__X+3, gstat->loc__Y + scaledY, currentcolor); // Left side line
	drawline(gstat->loc__X, gstat->loc__Y, gstat->loc__X + scaledX, gstat->loc__Y, currentcolor); // Top line
 	short calcheight = scaledY * disval / gstat->max - gstat->min; 	
	if (calcheight < 1) calcheight =1;
 	//float ch = (scaledY * (disval / (gstat->max - gstat->min))); 	
	fill_rect(gstat->loc__X + 4, gstat->loc__Y + (scaledY - (unsigned short)calcheight), scaledX, (unsigned short)calcheight, currentcolor);
	draw_numbers(gstat->loc__X + scaledX + 22, gstat->loc__Y + (scaledY - calcheight), 10, currentcolor, 0,  1, gstat->divider, disval); 
} 	
void display_bar__horizontil(struct template__gauge* gstat, unsigned short disval){
	// Here goes goes for an equally simple horizontil gauge display
	// Here goes goes for a vertical gauge display
	//deb887
	// here are the "extents" note: Some of this might be put in gstat build code instead of being constantly redone here.
	unsigned short scaledX = gstat->ratioXY[0] * gstat->scale;
	unsigned short scaledY = gstat->ratioXY[1] * gstat->scale;
	unsigned short currentcolor;
	if((disval >= gstat->criticallevel - gstat->criticalrange) &&  (disval <= gstat->criticallevel + gstat->criticalrange)) {  
		currentcolor = gstat->criticalcolor; 
	} else if ((disval >= gstat->warnlevel - gstat->warnrange) && (disval <= gstat->warnlevel + gstat->warnrange)){
		currentcolor = gstat->warncolor; 
	} else {
		currentcolor = gstat->greencolor; // Generally assume it's OK if warn and critical are not met
	}  // if
	draw_string(gstat->loc__X + gstat->titleoffset__X, gstat->loc__Y + gstat->titleoffset__Y, (char*)gstat->maintitle, gstat->titlelength, currentcolor, 0, gstat->titlespacing, gstat->titlefont);
	drawline(gstat->loc__X, gstat->loc__Y, gstat->loc__X ,gstat->loc__Y + scaledY, currentcolor); // Short line on the left 
	drawline(gstat->loc__X+1, gstat->loc__Y, gstat->loc__X+1,gstat->loc__Y + scaledY, currentcolor); // Short line on the left 
	drawline(gstat->loc__X, gstat->loc__Y +  scaledY, gstat->loc__X + scaledX, gstat->loc__Y +  scaledY, currentcolor); // long bottom line
	drawline(gstat->loc__X + scaledX, gstat->loc__Y, gstat->loc__X + scaledX, gstat->loc__Y + scaledY, currentcolor); // right line
	drawline(gstat->loc__X + scaledX+1, gstat->loc__Y, gstat->loc__X + scaledX+1, gstat->loc__Y + scaledY, currentcolor); // right line
 	short calclength = scaledX * disval / gstat->max - gstat->min; 	
	if (calclength < 1) calclength =1;
	fill_rect(gstat->loc__X, gstat->loc__Y -1 , scaledX-  (scaledX - (unsigned short)calclength), scaledY, currentcolor);
	draw_numbers(gstat->loc__X + (scaledX  +  (unsigned short)calclength) - scaledX, gstat->loc__Y + scaledY , 10, currentcolor, 0,  1, gstat->divider, disval); 
} 	

// done in the function. 
 // https://www.geeksforgeeks.org/nested-functions-c/
 // https://fresh2refresh.com/c-programming/c-nested-structure/
 //  //auto void never();
	//auto void never() {printf("hey now\n");}
	//scaledY = 177;	
	//never();
// The segmented bargraph display lends itself to different approaches. A static data structire is not needed, just some location
// and minor scaling.  Gauge template is still useful but uses also a structure of arrays to hold pre-calculated rendering data for speed. 
// The green zone is the center but green color is not entirely green. 
void bargraph__vertical(struct template__gauge* gstat, struct template__bargraph* bg, float deviation){
	unsigned short scaledX = gstat->ratioXY[0] * gstat->scale;
	unsigned short scaledY = gstat->ratioXY[1] * gstat->scale;
	unsigned short locY =  0;
	float devstat = (deviation / (float)gstat->max) * 12; // The "figurer" based on the number of bars plus 1, determines which is solid rect
	switch ((int)devstat) {  // additional text warning for the graph depending on where the value is - treat as optional
		case 12:
		case 11: locY = bg->loc__Ys[1];
			 draw_string(gstat->loc__X + scaledX + 10, locY, "CRITICAL", 8, gstat->criticalcolor, 0, 2, 1);
			 break;
		case 10: 
		case 9:  
		case 8: locY = bg->loc__Ys[3];
			draw_string(gstat->loc__X + scaledX + 10, locY, "WARNING", 7, gstat->warncolor, 0, 2, 1);
			break;
		case 7: 
		case 6: 
		case 5: locY = bg->loc__Ys[6];
			draw_string(gstat->loc__X + scaledX + 10, locY, "NOMINAL", 7, gstat->greencolor, 0, 2, 1);
			break;
		case 4: 
		case 3:  
		case 2: locY = bg->loc__Ys[9];
			draw_string(gstat->loc__X + scaledX + 10, locY, "WARNING", 7, gstat->warncolor, 0, 2, 1);
			break;
		case 1:
		case 0:  locY = bg->loc__Ys[11];
			 draw_string(gstat->loc__X + scaledX + 10, locY, "CRITICAL", 8, gstat->criticalcolor, 0, 2, 1);
			 break;
		default:
			break; 
	} ;
	// deb22a
	draw_string(gstat->loc__X + gstat->titleoffset__X, gstat->loc__Y + gstat->titleoffset__Y, (char*)gstat->maintitle, gstat->titlelength, gstat->greencolor, 0, gstat->titlespacing, gstat->titlefont);
//	draw_rect(gstat->loc__X, 
//		  gstat->loc__Y + gstat->titleoffset__Y, 
//		  scaledX, 
//		  scaledY, 
//		  gstat->greencolor, 5);
	(devstat >= 12)?fill_rect(gstat->loc__X, bg->loc__Ys[0], scaledX,  bg->spacing__Ys[0], gstat->criticalcolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[0], scaledX,  bg->spacing__Ys[0], gstat->criticalcolor, 1); 
	(devstat  > 11)?fill_rect(gstat->loc__X, bg->loc__Ys[1], scaledX,  bg->spacing__Ys[1], gstat->criticalcolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[1], scaledX,  bg->spacing__Ys[1], gstat->criticalcolor, 1); 
	(devstat  > 10)?fill_rect(gstat->loc__X, bg->loc__Ys[2], scaledX,  bg->spacing__Ys[2], gstat->warncolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[2], scaledX,  bg->spacing__Ys[2], gstat->warncolor, 1); 
	(devstat  >  9)?fill_rect(gstat->loc__X, bg->loc__Ys[3], scaledX,  bg->spacing__Ys[3], gstat->warncolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[3], scaledX,  bg->spacing__Ys[3], gstat->warncolor, 1); 
	(devstat  >  8)?fill_rect(gstat->loc__X, bg->loc__Ys[4], scaledX,  bg->spacing__Ys[4], gstat->warncolor): 
	                draw_rect(gstat->loc__X, bg->loc__Ys[4], scaledX,  bg->spacing__Ys[4], gstat->warncolor, 1); 
	(devstat  >  7)?fill_rect(gstat->loc__X, bg->loc__Ys[5], scaledX,  bg->spacing__Ys[5], gstat->greencolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[5], scaledX,  bg->spacing__Ys[5], gstat->greencolor, 1); 
	((devstat >= 6) || (devstat < 6))?fill_rect(gstat->loc__X, bg->loc__Ys[6], scaledX, bg->spacing__Ys[6], gstat->greencolor):  // the center
		       			  draw_rect(gstat->loc__X, bg->loc__Ys[6], scaledX, bg->spacing__Ys[6], gstat->greencolor, 1); 
	(devstat <   5)?fill_rect(gstat->loc__X, bg->loc__Ys[7], scaledX,  bg->spacing__Ys[7], gstat->greencolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[7], scaledX,  bg->spacing__Ys[7], gstat->greencolor, 1); 
	(devstat <   4)?fill_rect(gstat->loc__X, bg->loc__Ys[8], scaledX,  bg->spacing__Ys[8], gstat->warncolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[8], scaledX,  bg->spacing__Ys[8], gstat->warncolor, 1); 
	(devstat <   3)?fill_rect(gstat->loc__X, bg->loc__Ys[9], scaledX,  bg->spacing__Ys[9], gstat->warncolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[9], scaledX,  bg->spacing__Ys[9], gstat->warncolor, 1); 
	(devstat <   2)?fill_rect(gstat->loc__X, bg->loc__Ys[10], scaledX, bg->spacing__Ys[10], gstat->warncolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[10], scaledX, bg->spacing__Ys[10], gstat->warncolor, 1); 
	(devstat <   1)?fill_rect(gstat->loc__X, bg->loc__Ys[11], scaledX, bg->spacing__Ys[11], gstat->criticalcolor): 
		        draw_rect(gstat->loc__X, bg->loc__Ys[11], scaledX, bg->spacing__Ys[11], gstat->criticalcolor, 1); 
	(devstat <=  1)?fill_rect(gstat->loc__X, bg->loc__Ys[12], scaledX, bg->spacing__Ys[12], gstat->criticalcolor): 
		       draw_rect(gstat->loc__X,  bg->loc__Ys[12], scaledX, bg->spacing__Ys[12], gstat->criticalcolor, 1); 
}

// A basic and short means of getting data to the screen for quick messaging. Original concept for Fire warning system. A non-statistical display tool intended also for helping use controls. 
// deb707
void draw__ARMstat (unsigned short x, unsigned short y, char* banner, unsigned short bannerlength,  char* warntag, unsigned short taglength, unsigned short button, unsigned short color){
	//draw_rect(x - 20, y-20, taglength * 29, 100,  color, 2);
	draw_string(x, y, banner, bannerlength, color,  0, 10, 2);
	draw_string(x, y + 40, warntag, taglength, color,  0, 10, 2);
	drawline(x, y + 60, taglength , y, color);
 	unsigned short col = x;	
 	unsigned short row = y;
	unsigned short mark = 1;
	for (unsigned char incr = 0; incr < 4; incr++){
		for (unsigned char jctr = 0; jctr < 4; jctr++){
			if (mark == button){
				fill_rect(x + col, y + row, 77, 50, color); 
				draw_string(x, y  + row, "COUNTER ARMED", 13, color,  0, 5, 1);
				draw_string(x, y  + row + 20, "PRESS BUTTON TO ENGAGE", 22, color,  0, 5, 1);
			} else	if (mark == 16){
				draw_rect(x + col, y + row, 77, 50, color, 7);
			} else{
				draw_rect(x + col, y + row, 77, 50, color, 2);
			}  // if
			mark++;
			col += 88;
		}  // for
		col = x;
		row += 65;
	}  // for
}
void draw__BUTTONREF_small (unsigned short x, unsigned short y, unsigned short button, unsigned short color) {
 	unsigned int col = x;	
 	unsigned int row = y;
	unsigned short mark = 1;
	for (unsigned char incr = 0; incr < 4; incr++){
		for (unsigned char jctr = 0; jctr < 4; jctr++){
			if (mark == button){
				fill_rect(col, row, 10, 10, color); 
			} else	{
				draw_rect(col, row, 10, 10, color, 2);
			}   // if
			mark++;
			col += 12;
		}  // for
		col = x;
		row += 12;
	}  // for
}
void display_cube__systemstatus(struct template__SYSSTATCUBE* ssc){
	draw_rect(ssc->locX, ssc->locY, ssc->width, ssc->height, ssc->color, ssc->border);
	draw_string(ssc->locX + ssc->title_offsetX, ssc->locY + ssc->title_offsetY, ssc->title, ssc->title_length, ssc->color, 0, 10, 2); 
	draw_string(ssc->locX + ssc->status_offsetX, ssc->locY + ssc->status_offsetY, ssc->status, ssc->status_length, ssc->color, 0, 10, 2); 
	draw__BUTTONREF_small (ssc->locX +10, ssc->locY + ssc->height/2, ssc->buttonref, ssc->color);
}
void erase_cube(struct template__SYSSTATCUBE* ssc){
	clear_ROI(ssc->locX, ssc->locY, ssc->width, ssc->height, 0);
}

void get_O2status (unsigned short* o2scan) {   // this call is apt to require some reference to the memory location of the device that delivers to this deb444
 // The I2C device gives back a delimited string. 
 // the value that appears in the I2C device at 10 bit resolution is going to be 0 to 1024
 // This has to be interpolated to a value, after conversion to number, between 1 and 120 per the template given.
 // It would be best to use those values instead of hardcoding it. 
 // for example, if the value coming out of the call is 512,  512 / 1024 is .5. Multuply 120 by .5
 // Note: assuming 10 bit ADC
	char muhstuff[64] = "512,512,512,512,512,512,512,512";   // deb101 faking i2C call for now
	//char muhstuff[64] = "50,512,512,206,512,512,512,512";   // deb101 faking i2C call for now
	char* token = strtok(muhstuff, ",");
	for ( unsigned char incr = 0; incr < 8; incr++){
		o2scan[incr]  =  (unsigned short) atoi(token); // the ADC value from the microcontroller basically
		token = strtok(NULL, ",");
	}  // for 
//	return o2scan;
}
//  deb100  
//  Not  every system needs to be machine-gunned in a loop. Some take time to change. So the Priority variable is a countdown
//  that will let the check routine simply ignore running any routines to check the hardware/ports.
//  This frees up processes for other things that do require constant checking. 


// 02 system check routine
// This unit talks to another system that monitors O2 sensor arrays. Basically a simple routine using I2C to check on status, if OK returns, there is nothing more to do, or
// the routine can handle other tasks if anything other than OK comes back
unsigned short syscheck__O2(struct probes__O2* o2, unsigned short disp){
	if ((timer_global - o2->current > o2->priority) || o2->select == 1) {	
		get_O2status(o2->probestats);
		o2->current = time(NULL);
		if (o2->select) goto DISPARITY; // deb fake the display condition
		if((o2->probestats[0] < o2->lowADC ) || (o2->probestats[0] > 1020)) goto DISPARITY;
		if((o2->probestats[1] < o2->lowADC ) || (o2->probestats[1] > 1020)) goto DISPARITY;
		if((o2->probestats[2] < o2->lowADC ) || (o2->probestats[2] > 1020)) goto DISPARITY;
		if((o2->probestats[3] < o2->lowADC ) || (o2->probestats[3] > 1020)) goto DISPARITY;
		if((o2->probestats[4] < o2->lowADC ) || (o2->probestats[4] > 1020)) goto DISPARITY;
		if((o2->probestats[5] < o2->lowADC ) || (o2->probestats[5] > 1020)) goto DISPARITY;
		if((o2->probestats[6] < o2->lowADC ) || (o2->probestats[6] > 1020)) goto DISPARITY;
		if((o2->probestats[7] < o2->lowADC ) || (o2->probestats[7] > 1020)) goto DISPARITY;
			// having gotten this far, no disparity detected.
		setclear_priority_UNLOCK();
		o2->priority = 4;
	}  // if
	if (disp) {
		display_cube__systemstatus(&o2->disp__mainstat[0]);
		display_cube__systemstatus(&o2->disp__mainstat[1]);
		display_cube__systemstatus(&o2->disp__mainstat[2]);
		display_cube__systemstatus(&o2->disp__mainstat[3]);
		display_cube__systemstatus(&o2->disp__mainstat[4]);
		display_cube__systemstatus(&o2->disp__mainstat[5]);
		display_cube__systemstatus(&o2->disp__mainstat[6]);
		display_cube__systemstatus(&o2->disp__mainstat[7]);
	}  /*else if (disp == 0) {
		erase_cube(&o2->disp__mainstat[0]);
		erase_cube(&o2->disp__mainstat[1]);
		erase_cube(&o2->disp__mainstat[2]);
		erase_cube(&o2->disp__mainstat[3]);
		erase_cube(&o2->disp__mainstat[4]);
		erase_cube(&o2->disp__mainstat[5]);
		erase_cube(&o2->disp__mainstat[6]);
		erase_cube(&o2->disp__mainstat[7]);
	}// if */
	return 0;		// deb100 stub
DISPARITY:
	o2->priority = 0;
	setclear_priority_LOCK();
	return 1;
}

// Fire detection check routine
// Fire sensors are directly connected to the unit 
// The Fire Detection routine is unique in that the returning value reflects which sector of the engine bay was tripped as possibly having some flame.
unsigned short syscheck__FIREDETECT(struct detectors__FIRE *df, unsigned short disp) {
	// There is no priority communication directive used in this structure - even though it has the field. It's triggered by pyro sensors
	// ---deb8876	but that might be something to change later?
	// deb100 here would be the routines for checkiing GPIO pins seeing if the fire detectors
	// are sending in a high signal. 
	//printf ("deb101 FIRE timer = %i PRI = %i\n", timer_global - df->current, df->priority);
	// Fire can be fun and exciting, but it can also be dangerous.
	if(((*(gpio + GPIO_IN) & PYRO_1_IN) &  (1 << 28)) >> 28) {     // Engine bay Fire detected....
		df->priority = 0;
		setclear_priority_LOCK();
		return 1;
	}  // if
	if (timer_global - df->current > df->priority) {	
		df->current = time(NULL);
		df->priority = 2;
		setclear_priority_UNLOCK();
		// deb100 look for any issues here, return 1 if problem is found
	}  // if
	if (disp)
		display_cube__systemstatus(&df->disp__mainstat);
	//else
	//	erase_cube(&df->disp__mainstat);
	return 0;		// deb100 stub
}
// System Temperature status signal check
// This is a signal that originates from the unit and through a relay that closes when something is wrong with the Smartlight system. Smartlight works on its own but
// uses a secondary relay signal for any unit that monitors it.
unsigned short syscheck__TEMP (struct sensors__COOLANT* sc, unsigned short disp) {
	unsigned short res = 0;
	//printf ("deb101  TEMP timer = %i, PRI = %i\n", timer_global -  sc->current, sc->priority);
//	if (sc->comm_pri)return 1;
	if (timer_global - sc->current > sc->priority) {	
		// Check temp(s) here.
		// deb777-7   read ADC for temp here... ADC1
		// Note: there is also a GPIO for the Smartlight system to be used here.
		if (sc->temp__coolant_engine < sc->max_nominal) {	// Temp normal..
			sc->disp__mainstat.color =  base_color;
			sc->disp__mainstat.status = (char*)"OK";
			sc->current = time(NULL);
			sc->priority = 2;
			setclear_priority_UNLOCK();
		//	clear_ROI(sc->indicator.loc__X, 
		//		  sc->indicator.loc__Y, 
		//		  sc->indicator.scale * sc->indicator.ratioXY[0] + 77, 
		//	  	  sc->indicator.scale * sc->indicator.ratioXY[1] + 50, 
		//		  0);  
		} else {  // A condition with engine heat
			sc->disp__mainstat.color =  alert_color;
			sc->disp__mainstat.status = (char*)"OH";
			sc->priority = 0;
			//display_cube__systemstatus(&sc->disp__mainstat);
			setclear_priority_LOCK();
			res =  1;
		} // if
	}  // if
	if (disp)
		display_cube__systemstatus(&sc->disp__mainstat);
//	else
//		erase_cube(&sc->disp__mainstat);
	return res; 
}
// The transmission monitor system is going after mainly gears or temperature. Pressure may be possible with additional sensors later. 
//
unsigned short syscheck__TRANS(struct sensors__TRANS* st, unsigned short disp){
	//deb100 notes
	// THen check for gear. 
	// Pressure is left alone for now considering that it's not commonly monitored. 
	// CHECK for differences: Gear
	// 			  Temp In this case, if temp is over range
	// dem777-2   here possibly some i2c comms with a device that has simple digital ports for the transmission gear selection
	// 		and ADC2 for a transmission temp reading. 
	//		Or if something is going to be talking on i2c then use the ADC ports on that?
	//printf ("deb101  TRANS timer = %i, PRI = %i\n", timer_global -  st->current, st->priority);
//	if (st->comm_pri)return 1;
	if (timer_global - st->current > st->priority) {	
		st->current = time(NULL);
		// Here the checks are done, talking to hardware, other systems, etc. And reset the counter
		// Counter reset would also depend on situation. 
	 	st->priority = 1;	
		setclear_priority_UNLOCK();
	}  // if
	if(disp){
		display_cube__systemstatus(&st->disp__mainstat);
		if (st->current_gear != 'D') {
			//if (st->disp__mainstat == NULL) printf("deb342 why is that null?\n");
			st->disp__mainstat.color =  alert_color;
			//st->disp__mainstat.status = (char*)"R";
			//st->disp__mainstat.status_length = 1;
			draw_string(st->disp__mainstat.locX + st->disp__mainstat.status_offsetX, 
			    st->disp__mainstat.locY + st->disp__mainstat.title_offsetY * 3, 
			    (char*)"WARN", 
			    4, 
			    alert_color, 
			    0, 
			    13, 
			    1);
	 		st->priority = 1;	
		} else 	{
			st->disp__mainstat.color = base_color;
	 		st->priority = 100;	
			st->disp__mainstat.status = (char*)"D";
			st->disp__mainstat.status_length = 1;
			//clear_ROI(unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short c) {
			draw_string(st->disp__mainstat.locX + st->disp__mainstat.title_offsetX * 2, 
			    st->disp__mainstat.locY + st->disp__mainstat.title_offsetY * 3, 
			    (char*)"DRIVE", 
			    5, 
			    base_color, 
			    0, 
			    13, 
			    1);
		}  // if
//	} else {
//		erase_cube(&st->disp__mainstat);
	} // if
	return 0;		// deb100 stub : this routine returns 1 if there are any changes.
}
// The Fuel System is bacically a monitor for "bingo" operation, meaning, at half a tank similar to fighter jets.
unsigned short syscheck__FUEL(struct  sensors__FUEL *sf, unsigned short disp){
	//printf ("deb101  FUEL timer = %i, PRI = %i\n", timer_global -  sf->current, sf->priority);
//	if (sf->comm_pri)return 1;
//	deb777-3 Fuel represents a special issue whereby there is already a sensor and low voltage involved, roughly 5 volts, and that
//	would imply an ADC port. BBB ADC ports can't handle more than 1.8VDC so possibly something on I2C or a rail to rail - the latter hard to do on such low voltage
//	The way the common system works is that more fuel means less resistance in the sender and therefore less voltage drop. 
//	This the range of this voltage in an ADC port will go to whatever is being given to this circuit.  Ford systems for example use 6 volts.
	if (timer_global - sf->current > sf->priority) {	
		sf->current = time(NULL);
		sf->priority = 2;
		// deb100 look for any issues here, return 1 if problem is found
		priorityflag = 1;
		//setclear_priority_LOCK();
	}  // if
	if(disp)
		display_cube__systemstatus(&sf->disp__mainstat);
//	else
//		erase_cube(&sf->disp__mainstat);
	//deb555 if there is an issue then 1 is returned instead of 0
	return 0;		// deb100 stub
}
// The most important thing for oil is pressure, but temperature can be meaningful. Oil at the wrong temp can be trouble on the long term.
// This is merely a monitor for oil pressure, showing if it's too low or even too high. 
unsigned short syscheck__OIL(struct sensors__OIL *so, unsigned short disp){
	// deb777-4 Oil has temperature and pressure, two things that take up ADC ports. Possibly an I2C based monitor? Share with Transmission? 
	//printf ("deb101  OIL timer = %i, PRI = %i\n", timer_global -  so->current, so->priority);
//	if (so->comm_pri)return 1;
	if (timer_global - so->current > so->priority) {	
		so->current = time(NULL);
		so->priority = 1;
		// deb100 look for any issues here, return 1 if problem is found
		//priorityflag = 1;
		//clearflag = 1;
	}  // if
	if (disp)
		display_cube__systemstatus(&so->disp__mainstat);
//	else
//		erase_cube(&so->disp__mainstat);
	//deb555 if there is an issue then 1 is returned instead of 0
	return 0;		// deb100 stub
}




// application entry point
//
//	===========================================================================================================\
// =============================================================================================================\
// =============================================================================================================/
// ============================================================================================================/
//
//
int main(int argc, char* argv[])
{


	timer_global = time(NULL);
	// deb774 stuff that must be persistent AFTER the construction block must be declared before it!

	struct template__gauge *tg;
	struct template__bargraph *bg; 



	struct sensors__COOLANT* system__cooling;		//deb111
	struct sensors__OIL* system__oil;		//deb111
	struct sensors__TRANS* system__trans;  //deb111
	struct sensors__FUEL* system__fuel;
	struct detectors__FIRE* system__firecontrol;
	struct probes__O2* system__O2probes; 
 	// The actual glyphs here. Discard that which is not used to save memory. This is a "construction block"
	{  // start construction block >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
		unsigned char A__10x14[CHAR_ARR__10x14] = {
											0,0,0,0,1,1,0,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,1,1,1,1,1,1,0,0,
											0,1,1,1,0,0,1,1,1,0,
											1,1,1,0,0,0,0,1,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,1,1,1,1,1,1,1,1,
											1,1,1,1,1,1,1,1,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,0,0,0,0,0,0,1,1,
											1,1,0,0,0,0,0,0,1,1
		};
		unsigned char A__29x24[CHAR_ARR__29x24] = {
									 0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
									 0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
									 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
									 0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
									 0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
									 0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
									 0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
									 0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
									 0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
									 0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
									 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
									 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
									 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
									 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char B__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,0,0,0,
													1,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,1,
													1,1,1,1,1,1,1,1,1,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													1,1,1,1,1,1,1,0,0,0
		};

		unsigned char B__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char C__10x14[CHAR_ARR__10x14] = {
													0,0,0,1,1,1,1,1,0,0,
													0,0,1,1,1,1,1,1,0,0,
													1,1,1,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,0,
													0,0,1,1,1,1,1,1,0,0,
													0,0,0,1,1,1,1,1,0,0
		};
		unsigned char C__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char D__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,0,0,0,
													1,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,1,
													1,1,1,1,1,1,1,0,0,0
		};
		unsigned char D__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char E__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1
		};
		unsigned char E__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char F__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0
		};
		unsigned char F__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char G__10x14[CHAR_ARR__10x14] = {
													0,0,0,1,1,1,1,1,0,0,
													0,0,1,1,1,1,1,1,0,0,
													1,1,1,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,1,1,1,1,1,
													1,1,0,0,0,1,1,1,1,1,
													1,1,1,0,0,0,0,1,1,1,
													0,0,1,1,1,1,1,1,0,0,
													0,0,0,1,1,1,1,1,0,0
		};
		unsigned char G__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char H__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char H__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char I__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1
		};
		unsigned char I__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char J__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													1,1,0,0,1,1,0,0,0,0,
													1,1,0,0,1,1,0,0,0,0,
													0,1,1,1,1,1,0,0,0,0,
													0,0,1,1,1,0,0,0,0,0
		};
		unsigned char J__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char K__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,1,1,0,0,
													1,1,0,0,0,1,1,0,0,0,
													1,1,0,0,1,1,0,0,0,0,
													1,1,1,1,1,0,0,0,0,0,
													1,1,1,1,0,0,0,0,0,0,
													1,1,1,1,0,0,0,0,0,0,
													1,1,1,1,0,0,0,0,0,0,
													1,1,0,1,1,0,0,0,0,0,
													1,1,0,0,1,1,0,0,0,0,
													1,1,0,0,0,0,1,1,0,0,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char K__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0
		};
		unsigned char L__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1
		};
		unsigned char L__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char M__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,1,
													1,1,1,1,0,0,1,1,1,1,
													1,1,0,1,1,1,1,0,1,1,
													1,1,0,0,1,1,0,0,1,1,
													1,1,0,0,1,1,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char M__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,0,1,1,1,1,
								   1,1,1,1,0,0,1,1,1,1,1,0,0,0,1,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char N__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,0,1,1,
													1,1,1,1,0,0,0,0,1,1,
													1,1,1,1,1,0,0,0,1,1,
													1,1,0,1,1,1,0,0,1,1,
													1,1,0,0,1,1,1,0,1,1,
													1,1,0,0,0,1,1,1,1,1,
													1,1,0,0,0,0,1,1,1,1,
													1,1,0,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char N__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char O__10x14[CHAR_ARR__10x14] = {
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,1,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,1,
													0,1,1,1,1,1,1,1,1,0,
													0,0,1,1,1,1,1,1,0,0
		};
		unsigned char O__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char P__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0
		};
		unsigned char P__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char Q__10x14[CHAR_ARR__10x14] = {
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,1,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,1,1,1,0,1,1,
													1,1,0,0,0,1,1,1,1,1,
													1,1,1,0,0,0,1,1,1,1,
													0,1,1,1,1,1,1,1,1,1,
													0,0,1,1,1,1,1,1,1,1
		};

		unsigned char Q__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char R__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,0,0,0,
													1,1,0,1,1,1,0,0,0,0,
													1,1,0,0,0,1,1,0,0,0,
													1,1,0,0,0,0,1,1,0,0,
													1,1,0,0,0,0,0,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char R__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char S__10x14[CHAR_ARR__10x14] = {
													0,0,0,1,1,1,1,0,0,0,
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,0,0,0,1,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,0,0,0,0,0,0,0,
													0,1,1,1,1,1,1,0,0,0,
													0,0,1,1,1,1,1,1,0,0,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,0,
													0,0,1,1,1,1,1,1,0,0,
													0,0,0,1,1,1,1,0,0,0
		};
		unsigned char S__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char T__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0
		};
		unsigned char T__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char U__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,0,0,0,0,1,1,0,
													0,0,1,1,1,1,1,1,0,0,
													0,0,0,1,1,1,1,0,0,0
		};

		unsigned char U__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char V__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,0,0,0,0,1,1,0,
													0,1,1,0,0,0,0,1,1,0,
													0,0,1,1,0,0,1,1,0,0,
													0,0,0,1,1,1,1,0,0,0,
													0,0,0,0,1,1,0,0,0,0
		};
		unsigned char V__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char W__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,1,1,0,0,1,1,
													1,1,0,1,1,1,1,0,1,1,
													1,1,0,1,1,1,1,0,1,1,
													1,1,1,1,0,0,1,1,1,1,
													1,1,1,0,0,0,1,1,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char W__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,1,1,1,1,0,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,1,1,1,1,1,0,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1
		};
		unsigned char X__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,1,
													0,1,1,1,0,0,1,1,1,0,
													0,0,1,1,0,0,1,1,0,0,
													0,0,0,1,1,1,1,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,1,1,1,1,0,0,0,
													0,0,1,1,0,0,1,1,0,0,
													0,1,1,1,0,0,1,1,1,1,
													1,1,1,0,0,0,0,1,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1
		};
		unsigned char X__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1
		};
		unsigned char Y__10x14[CHAR_ARR__10x14] = {
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,1,0,0,0,0,1,1,1,
													0,1,1,1,0,0,1,1,1,0,
													0,0,1,1,0,0,1,1,0,0,
													0,0,0,1,1,1,1,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0
		};
		unsigned char Y__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char Z__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													0,1,1,0,0,0,0,0,0,0,
													0,0,1,1,0,0,0,0,0,0,
													0,0,0,1,1,0,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,0,1,1,0,0,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,0,0,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1
		};
		unsigned char Z__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char AR1__10x14[CHAR_ARR__10x14] = {
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,1,1,1,0,0,0,0,
													0,0,1,1,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,1,1,1,1,1,1,0,0,
													0,0,1,1,1,1,1,1,0,0
		};
		unsigned char AR1__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char AR2__10x14[CHAR_ARR__10x14] = {
													0,0,0,1,1,1,1,0,0,0,
													0,1,1,1,1,1,1,1,0,0,
													1,1,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,1,1,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,1,1,0,0,0,0,0,
													0,0,1,1,0,0,0,0,0,0,
													0,1,1,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1
		};

		unsigned char AR2__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char AR3__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,1,1,0,0,0,
													0,0,0,0,1,1,1,1,0,0,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													1,1,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,0,0,0
		};
		unsigned char AR3__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char AR4__10x14[CHAR_ARR__10x14] = {
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,1,1,1,0,0,
													0,0,0,0,1,1,1,1,0,0,
													0,0,0,1,1,0,1,1,0,0,
													0,0,1,1,0,0,1,1,0,0,
													0,1,1,0,0,0,1,1,0,0,
													1,1,0,0,0,0,1,1,0,0,
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,0,1,1,0,0
		};
		unsigned char AR4__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0
		};
		unsigned char AR5__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,0,0,0,
													1,1,1,1,1,1,1,1,0,0,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,1,1,0,
													0,1,1,1,1,1,1,1,0,0,
													0,0,1,1,1,1,1,0,0,0
		};
		unsigned char AR5__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char AR6__10x14[CHAR_ARR__10x14] = {
													0,0,0,0,0,1,1,1,1,1,
													0,0,0,0,1,1,1,1,1,1,
													0,0,0,1,1,0,0,0,0,0,
													0,0,1,1,0,0,0,0,0,0,
													0,1,1,0,0,0,0,0,0,0,
													1,1,0,0,0,0,0,0,0,0,
													1,1,1,1,1,1,1,1,0,0,
													1,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,1,1,1,1,1,1,0,
													0,0,1,1,1,1,1,1,0,0
		};
		unsigned char AR6__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		unsigned char AR7__10x14[CHAR_ARR__10x14] = {
													1,1,1,1,1,1,1,1,1,1,
													1,1,1,1,1,1,1,1,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,0,1,1,
													0,0,0,0,0,0,0,1,1,0,
													0,0,0,0,0,0,1,1,0,0,
													0,0,0,0,0,1,1,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0,
													0,0,0,0,1,1,0,0,0,0
		};
		unsigned char AR7__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char AR8__10x14[CHAR_ARR__10x14] = {
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,1,1,1,1,1,1,0,
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,1,1,1,1,1,1,0,
													0,0,1,1,1,1,1,1,0,0
		};
		unsigned char AR8__29x24[CHAR_ARR__29x24] = {
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0
		};
		unsigned char AR9__10x14[CHAR_ARR__10x14] = {
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													1,1,0,0,0,0,0,0,1,1,
													0,1,1,1,1,1,1,1,1,1,
													0,0,1,1,1,1,1,1,1,1,
													0,0,0,0,0,0,0,1,1,1,
													0,0,0,0,0,0,0,1,1,1,
													0,0,0,0,0,0,1,1,1,0,
													0,0,0,0,0,1,1,1,0,0,
													0,0,0,0,1,1,1,0,0,0,
													1,1,1,1,1,1,0,0,0,0,
													1,1,1,1,1,0,0,0,0,0
		};
		unsigned char AR9__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char AR0__10x14[CHAR_ARR__10x14] = {
													0,0,1,1,1,1,1,1,0,0,
													0,1,1,1,1,1,1,1,1,0,
													1,1,0,0,0,0,1,1,1,1,
													1,1,0,0,0,0,1,1,1,1,
													1,1,0,0,0,1,1,1,1,1,
													1,1,0,0,0,1,1,0,1,1,
													1,1,0,0,1,1,0,0,1,1,
													1,1,0,0,1,1,0,0,1,1,
													1,1,0,1,1,0,0,0,1,1,
													1,1,1,1,1,0,0,0,1,1,
													1,1,1,1,0,0,0,0,1,1,
													1,1,1,1,0,0,0,0,1,1,
													0,1,1,1,1,1,1,1,1,0,
													0,0,1,1,1,1,1,1,0,0
		};
		unsigned char AR0__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,
								   1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,
								   1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,
								   1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,
								   1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
								   0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
								   0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
								   0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
								   0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0
		};
		// specials
		unsigned char COLON__10x14[CHAR_ARR__10x14] = {
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,0,0,0,0,0,0,0,
											0,0,0,0,0,0,0,0,0,0,
											0,0,0,0,0,0,0,0,0,0,
											0,0,0,0,0,0,0,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0,
											0,0,0,1,1,1,1,0,0,0
		};
		unsigned char COLON__29x24[CHAR_ARR__29x24] = {
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};
		unsigned char LeftBracket__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};
		unsigned char RightBracket__29x24[CHAR_ARR__29x24] = {
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
								   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
		};

		unsigned char SPACE__10x14[CHAR_ARR__10x14] = {
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0,
													0,0,0,0,0,0,0,0,0,0
		};
		unsigned char SPACE__29x24[CHAR_ARR__29x24] = {
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
						   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
		};


		// Elements actually correspond to the ASCII chart 
		ascii_characters_BIG[32] = SPACE__29x24;
		ascii_characters_BIG[48] = AR0__29x24;
		ascii_characters_BIG[49] = AR1__29x24;
		ascii_characters_BIG[50] = AR2__29x24;
		ascii_characters_BIG[51] = AR3__29x24;
		ascii_characters_BIG[52] = AR4__29x24;
		ascii_characters_BIG[53] = AR5__29x24;
		ascii_characters_BIG[54] = AR6__29x24;
		ascii_characters_BIG[55] = AR7__29x24;
		ascii_characters_BIG[56] = AR8__29x24;
		ascii_characters_BIG[57] = AR9__29x24;
		ascii_characters_BIG[58] = COLON__29x24;
		ascii_characters_BIG[65] = A__29x24;
		ascii_characters_BIG[66] = B__29x24;
		ascii_characters_BIG[67] = C__29x24;
		ascii_characters_BIG[68] = D__29x24;
		ascii_characters_BIG[69] = E__29x24;
		ascii_characters_BIG[70] = F__29x24;
		ascii_characters_BIG[71] = G__29x24;
		ascii_characters_BIG[72] = H__29x24;
		ascii_characters_BIG[73] = I__29x24;
		ascii_characters_BIG[74] = J__29x24;
		ascii_characters_BIG[75] = K__29x24;
		ascii_characters_BIG[76] = L__29x24;
		ascii_characters_BIG[77] = M__29x24;
		ascii_characters_BIG[78] = N__29x24;
		ascii_characters_BIG[79] = O__29x24;
		ascii_characters_BIG[80] = P__29x24;
		ascii_characters_BIG[81] = Q__29x24;
		ascii_characters_BIG[82] = R__29x24;
		ascii_characters_BIG[83] = S__29x24;
		ascii_characters_BIG[84] = T__29x24;
		ascii_characters_BIG[85] = U__29x24;
		ascii_characters_BIG[86] = V__29x24;
		ascii_characters_BIG[87] = W__29x24;
		ascii_characters_BIG[88] = X__29x24;
		ascii_characters_BIG[89] = Y__29x24;
		ascii_characters_BIG[90] = Z__29x24;

		ascii_characters_SMALL[32] = SPACE__10x14;
		ascii_characters_SMALL[48] = AR0__10x14;
		ascii_characters_SMALL[49] = AR1__10x14;
		ascii_characters_SMALL[50] = AR2__10x14;
		ascii_characters_SMALL[51] = AR3__10x14;
		ascii_characters_SMALL[52] = AR4__10x14;
		ascii_characters_SMALL[53] = AR5__10x14;
		ascii_characters_SMALL[54] = AR6__10x14;
		ascii_characters_SMALL[55] = AR7__10x14;
		ascii_characters_SMALL[56] = AR8__10x14;
		ascii_characters_SMALL[57] = AR9__10x14;
		ascii_characters_SMALL[58] = COLON__10x14;
		ascii_characters_SMALL[65] = A__10x14;
		ascii_characters_SMALL[66] = B__10x14;
		ascii_characters_SMALL[67] = C__10x14;
		ascii_characters_SMALL[68] = D__10x14;
		ascii_characters_SMALL[69] = E__10x14;
		ascii_characters_SMALL[70] = F__10x14;
		ascii_characters_SMALL[71] = G__10x14;
		ascii_characters_SMALL[72] = H__10x14;
		ascii_characters_SMALL[73] = I__10x14;
		ascii_characters_SMALL[74] = J__10x14;
		ascii_characters_SMALL[75] = K__10x14;
		ascii_characters_SMALL[76] = L__10x14;
		ascii_characters_SMALL[77] = M__10x14;
		ascii_characters_SMALL[78] = N__10x14;
		ascii_characters_SMALL[79] = O__10x14;
		ascii_characters_SMALL[80] = P__10x14;
		ascii_characters_SMALL[81] = Q__10x14;
		ascii_characters_SMALL[82] = R__10x14;
		ascii_characters_SMALL[83] = S__10x14;
		ascii_characters_SMALL[84] = T__10x14;
		ascii_characters_SMALL[85] = U__10x14;
		ascii_characters_SMALL[86] = V__10x14;
		ascii_characters_SMALL[87] = W__10x14;
		ascii_characters_SMALL[88] = X__10x14;
		ascii_characters_SMALL[89] = Y__10x14;
		ascii_characters_SMALL[90] = Z__10x14;

		numbers_small[0] = AR0__10x14;	// For number displays
		numbers_small[1] = AR1__10x14;
		numbers_small[2] = AR2__10x14;
		numbers_small[3] = AR3__10x14;
		numbers_small[4] = AR4__10x14;
		numbers_small[5] = AR5__10x14;
		numbers_small[6] = AR6__10x14;
		numbers_small[7] = AR7__10x14;
		numbers_small[8] = AR8__10x14;
		numbers_small[9] = AR9__10x14;

		numbers_BIG[0] = AR0__29x24;
		numbers_BIG[1] = AR1__29x24;
		numbers_BIG[2] = AR2__29x24;
		numbers_BIG[3] = AR3__29x24;
		numbers_BIG[4] = AR4__29x24;
		numbers_BIG[5] = AR5__29x24;
		numbers_BIG[6] = AR6__29x24;
		numbers_BIG[7] = AR7__29x24;
		numbers_BIG[8] = AR8__29x24;
		numbers_BIG[9] = AR9__29x24;
	//deb777
	// Auto-assign way  - less coding - using the "constructor" approach in C with structures 
	// Note the scope block. Things not pointed to at the end of this are "lost". In some cases, uses two of these back to back cause the pointed-to
	// structures to become corrupted. It's an odd effect so far seen on a Debian install of Beaglebone Black. It's best to use one 
	// "construction block" like this anyway.
	// NOTE::: On Beaglebone, more than one construction block does not work. 
	//
	//
	//
	//
		// COOLING indication build
		struct template__gauge t_sst = {
			10, // X location
			200, // y location
			"TEMP", // title
			2,  // title font size
			4,  // title length
			5,  // title spacing
			0, // title offset x
			200,   // title offset y
			10,   // minimum value
			260, // maximum value
			212, // warn level
			20,  // warn range
			166,   // warncolor
			230, // critical level
			10,  // critical range
			224,   // critical color
			190,  // greenzone
			20,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		struct template__SYSSTATCUBE sst = {"COOLING",7, "OK", 2, 10,  200, 15, 20, 150, 80, 250, 120, 4, base_color, 4};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", sst.locX, sst.locY, sst.width, sst.height, sst.color, sst.border);
		system__cooling = (struct sensors__COOLANT*)malloc(sizeof(struct sensors__COOLANT));   // allocate for total struct
		struct sensors__COOLANT sct;//  = { 0, 0, 0, &sst, &t};
		sct.priority = 0;
		sct.select = 0;
		sct.current = time(NULL);
		sct.temp__coolant_upstream = 0;
		sct.temp__coolant_downstream =0 ;
		sct.temp__coolant_engine = 0;
		sct.disp__mainstat = sst;
		sct.indicator = t_sst;		// assign here.
		sct.indicator.loc__X = 100; //sst.locX;
		sct.indicator.loc__Y =sst.locY + 250; 
		sct.indicator.titleoffset__X = 50;
		sct.indicator.titleoffset__Y = 200;
		sct.indicator.maintitle = "TEMP";
		sct.indicator.titlelength = 4;
		sct.indicator.min = 30;
		sct.indicator.max = 260;
		sct.indicator.warnlevel = 230;
		sct.indicator.warnrange = 15;
		sct.indicator.criticallevel = 260;
		sct.indicator.criticalrange  = 15;
		sct.indicator.greenzone = 210;
		sct.indicator.greenrange = 20;
		sct.indicator.scale = 100;
		sct.indicator.divider = 1000; 
		sct.max_nominal = 222;
		sct.min_nominal = 180;
		system__cooling = &sct;
		//system__cooling->temp__coolant_engine = 260;  //deb100
		

		//printf("deb4i55 - in cube creation routines...%i, %i, %i, %i, %i, %i\n", system__cooling->disp__mainstat->locX, sst.locY, sst.width, sst.height, sst.color, sst.border);


		// OIL indication build
		struct template__gauge t_sso = {
			10, // X location
			200, // y location
			"OIL PRESSURE", // title
			2,  // title font size
			12,  // title length
			5,  // title spacing
			0, // title offset x
			200,   // title offset y
			20,   // minimum value
			100, // maximum value
			65, // warn level
			20,  // warn range
			166,   // warncolor
			85, // critical level
			10,  // critical range
			224,   // critical color
			40,  // greenzone
			20,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		struct template__SYSSTATCUBE ssot = {"OIL",3, "OK", 2, 270,  200, 15, 20, 150, 80, 250, 120, 4, base_color, 5};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", ssot.locX, ssot.locY, ssot.width, ssot.height, ssot.color, ssot.border);
		system__oil = (struct sensors__OIL*)malloc(sizeof(struct sensors__OIL));   // allocate for total struct
	//	system__oil->disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within

		struct sensors__OIL sot;//  = {  0, 0, &sst, &t};
		//sot.disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within
		sot.priority = 0;
		sot.select = 0;
		sot.current = time(NULL);
		sot.pressure = 0;
		sot.temperature = 0 ;
		sot.disp__mainstat = ssot;
		sot.indicator = t_sso;			// Assign gauge template here.
		sot.indicator.loc__X = sst.locX;
		sot.indicator.loc__Y =sst.locY + 250; 
		sot.indicator.titleoffset__X = 50;
		sot.indicator.titleoffset__Y = 200;
		sot.indicator.maintitle = "OIL";
		sot.indicator.titlelength = 3;
		sot.indicator.min = 30;
		sot.indicator.max = 260;
		sot.indicator.warnlevel = 230;
		sot.indicator.warnrange = 15;
		sot.indicator.criticallevel = 260;
		sot.indicator.criticalrange  = 15;
		sot.indicator.greenzone = 210;
		sot.indicator.greenrange = 20;
		sot.indicator.scale = 100;
		sot.indicator.divider = 1000; 
		system__oil = &sot;

		// TRANS indication build
		struct template__gauge t_sstr = {
			10, // X location
			200, // y location
			"TRANS PRESSURE", // title
			2,  // title font size
			14,  // title length
			5,  // title spacing
			0, // title offset x
			200,   // title offset y
			20,   // minimum value
			100, // maximum value
			65, // warn level
			20,  // warn range
			166,   // warncolor
			85, // critical level
			10,  // critical range
			224,   // critical color
			40,  // greenzone
			20,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		struct template__SYSSTATCUBE sstt = {"TRANS",5, "OK", 2, 530, 200, 15, 20, 150, 80, 250, 120, 4, base_color, 6};
		printf("deb444--892673894623842 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", sstt.locX, sstt.locY, sstt.width, sstt.height, sstt.color, sstt.border);
		system__trans = (struct sensors__TRANS*)malloc(sizeof(struct sensors__TRANS));   // allocate for total struct
		//system__trans->disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within

		struct sensors__TRANS stt;//  = {  0, 0, &sst, &t};
//		stt.disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within
		stt.priority = 0;
		stt.select = 0;
		stt.current = time(NULL);
		stt.current_gear = 'P'; // Presume power up at park condition but the monitor loop would change this soon enough. 
		stt.trans_temp = 0;
		stt.trans_press = 0;
		stt.disp__mainstat = sstt;
		stt.indicator = t_sstr;			// Aassign gauge template here. 
		stt.indicator.loc__X = sst.locX;
		stt.indicator.loc__Y =sst.locY + 250; 
		stt.indicator.titleoffset__X = 50;
		stt.indicator.titleoffset__Y = 200;
		stt.indicator.maintitle = "TRANS";
		stt.indicator.titlelength = 5;
		stt.indicator.loc__X = sstt.locX;
		stt.indicator.loc__Y = sstt.locY + 400;
		system__trans = &stt;


		// FUEL indication build
		struct template__gauge t_ssf = {
			10, // X location
			200, // y location
			"FUEL", // title
			2,  // title font size
			4,  // title length
			5,  // title spacing
			0, // title offset x
			200,   // title offset y
			1,   // minimum value
			18, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		struct template__SYSSTATCUBE ssft = {"FUEL",4, "OK", 2, 790, 200, 15, 20, 150, 80, 250, 120, 4, base_color, 7};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", ssft.locX, ssft.locY, ssft.width, ssft.height, ssft.color, ssft.border);
		system__fuel = (struct sensors__FUEL*)malloc(sizeof(struct sensors__FUEL));   // allocate for total struct
		//system__fuel->disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within

		struct sensors__FUEL sft;//  = {  0, 0, &sst, &t};
//		sft.disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within
		sft.priority = 0;
		sft.select = 0;
		sft.current = time(NULL);
		sft.gallons = 0;
		sft.max_gallons = 18;	// Check with the stats on the vehicle to see what this is
		sft.pressure = 0;
		sft.status = ' ';
		sft.disp__mainstat = ssft;
		sft.indicator = t_ssf;		// Assigne gauge template here.
		sft.indicator.maintitle = "FUEL";
		sft.indicator.titlelength = 4;
		sft.indicator.loc__X = ssft.locX;
		sft.indicator.loc__Y = ssft.locY + 400;
		sft.indicator.min = 30;
		sft.indicator.max = 260;
		sft.indicator.warnlevel = 230;
		sft.indicator.warnrange = 15;
		sft.indicator.criticallevel = 260;
		sft.indicator.criticalrange  = 15;
		sft.indicator.greenzone = 210;
		sft.indicator.greenrange = 20;
		sft.indicator.scale = 100;
		sft.indicator.divider = 1000; 
		system__fuel = &sft;


		// FIRE detection build
		// Fire detection and control does not have a gauge indication.
		struct template__SYSSTATCUBE ssfd = {"FIRE",4, "OK", 2, 1050, 200, 15, 20, 150, 80, 250, 120, 4, base_color, 8};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", ssfd.locX, ssfd.locY, ssfd.width, ssfd.height, ssfd.color, ssfd.border);
		system__firecontrol = (struct detectors__FIRE*)malloc(sizeof(struct detectors__FIRE));   // allocate for total struct
		//system__firecontrol->disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within

		//deb654 - this is going to be different for FIRE

		struct detectors__FIRE  sff;
//		sff.disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within
		sff.priority = 0;
		sff.current = time(NULL);
		sff.quadrant = 0;
		sff.status = (char*)"CLEAR";
		sff.disp__mainstat = ssfd;
		system__firecontrol = &sff;

		// O2 sensors build
		//struct template__SYSSTATCUBE sfod = {"AIR FUEL", 8, "OK", 2, 10, 470, 15, 20, 150, 80, 250, 120, 4, base_color, 8};
		//printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, %i, %i, %i, %i, %i\n", sfod.locX, sfod.locY, sfod.width, sfod.height, sfod.color, sfod.border);
		system__O2probes = (struct probes__O2*)malloc(sizeof(struct probes__O2));   // allocate for total struct


		struct probes__O2  sfo;
	//	sfo.disp__mainstat = (struct template__SYSSTATCUBE*)malloc(sizeof(struct template__SYSSTATCUBE));  // the structs within
		sfo.priority = 4;  // O2 probes take time to heat up.
		sfo.select = 0;
		sfo.current = time(NULL);
		sfo.disp__mainstat[0] = (struct template__SYSSTATCUBE){"CYL 1", 5, "OK", 2, 10,  340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[0].locX);
		sfo.disp__mainstat[1] = (struct template__SYSSTATCUBE){"CYL 2", 5, "OK", 2, 270, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[1].locX);
		sfo.disp__mainstat[2] = (struct template__SYSSTATCUBE){"CYL 3", 5, "OK", 2, 530, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[2].locX);
		sfo.disp__mainstat[3] = (struct template__SYSSTATCUBE){"CYL 4", 5, "OK", 2, 800, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[3].locX);
		sfo.disp__mainstat[4] = (struct template__SYSSTATCUBE){"CYL 5", 5, "OK", 2, 1070, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[4].locX);
		sfo.disp__mainstat[5] = (struct template__SYSSTATCUBE){"CYL 6", 5, "OK", 2, 1340, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[5].locX);
		sfo.disp__mainstat[6] = (struct template__SYSSTATCUBE){"CYL 7", 5, "OK", 2, 1610, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[6].locX);
		sfo.disp__mainstat[7] = (struct template__SYSSTATCUBE){"CYL 8", 5, "OK", 2, 1880, 340, 15, 20, 150, 80, 250, 120, 4, base_color, 9};
		printf("deb444 - in cube creation routines..without this values are lost??????????????\n.%i, \n", sfo.disp__mainstat[7].locX);
		unsigned short ymod = 150;
		struct template__gauge t_sso2_1 = {
			sfo.disp__mainstat[0].locX + 20, // X location
			sfo.disp__mainstat[0].locY + ymod, // Y location
			"CYL 1", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[0] = t_sso2_1;
		printf("max %i\n", sfo.indicators[0].max);
		struct template__gauge t_sso2_2 = {
			sfo.disp__mainstat[1].locX + 20, // X location
			sfo.disp__mainstat[1].locY + ymod, // Y location
			"CYL 2", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[1] = t_sso2_2;
		printf("max %i\n", sfo.indicators[1].max);
		struct template__gauge t_sso2_3 = {
			sfo.disp__mainstat[2].locX + 20, // X location
			sfo.disp__mainstat[2].locY + ymod, // Y location
			"CYL 3", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[2] = t_sso2_3;
		printf("max %i\n", sfo.indicators[2].max);
		struct template__gauge t_sso2_4 = {
			sfo.disp__mainstat[3].locX + 20, // X location
			sfo.disp__mainstat[3].locY + ymod, // Y location
			"CYL 4", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[3] = t_sso2_4;
		printf("max %i\n", sfo.indicators[3].max);
		struct template__gauge t_sso2_5 = {
			sfo.disp__mainstat[4].locX + 20, // X location
			sfo.disp__mainstat[4].locY + ymod, // Y location
			"CYL 5", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[4] = t_sso2_5;
		printf("max %i\n", sfo.indicators[4].max);
		struct template__gauge t_sso2_6 = {
			sfo.disp__mainstat[5].locX + 20, // X location
			sfo.disp__mainstat[5].locY + ymod, // Y location
			"CYL 6", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[5] = t_sso2_6;
		printf("max %i\n", sfo.indicators[5].max);
		struct template__gauge t_sso2_7 = {
			sfo.disp__mainstat[6].locX + 20, // X location
			sfo.disp__mainstat[6].locY + ymod, // Y location
			"CYL 7", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[6] = t_sso2_7;
		printf("max %i\n", sfo.indicators[6].max);
		struct template__gauge t_sso2_8 = {
			sfo.disp__mainstat[7].locX + 20, // X location
			sfo.disp__mainstat[7].locY + ymod, // Y location
			"CYL 8", // title
			2,  // title font size
			5,  // title length
			5,  // title spacing
			0, // title offset x
			65,   // title offset y
			50,   // minimum value
			1024, // maximum value
			1, // warn level
			1,  // warn range
			166,   // warncolor
			0, // critical level
			10,  // critical range
			224,   // critical color
			10,  // greenzone
			8,  // greenrange
			7, // green color
			51,  // scale Note for bar graph, come up with scale and ratio for X that comes to odd number to avoid rectangle clipping
			{3,2},// size ratio
			100};// divider for number display - important: get this wrong and no number will print
		sfo.indicators[7] = t_sso2_8;
		printf("max %i\n", sfo.indicators[7].max);
	        unsigned short	rectdiv = t_sso2_8.ratioXY[1]; // this is the "bar stagger" control - use carefully it can run away and cause segfault.
	        unsigned short locY = sfo.disp__mainstat[0].locY; // this actually progresses
		struct template__bargraph a = { 
					{
						 locY,
						 locY += 11 * rectdiv,
						 locY += 10 * rectdiv,
						 locY += 9 * rectdiv,
						 locY += 8 * rectdiv,
						 locY += 7 * rectdiv,
						 locY += 6 * rectdiv,
						 locY += 5 * rectdiv,
						 locY += 6 * rectdiv,
						 locY += 7 * rectdiv,
						 locY += 8 * rectdiv,
						 locY += 9 * rectdiv,
						 locY += 10 * rectdiv
						},
						{
						 rectdiv * 11 / 2, 
						 rectdiv * 10 / 2,
						 rectdiv * 9 / 2,
						 rectdiv * 8 / 2,
						 rectdiv * 7 / 2,
						 rectdiv * 6 / 2,
						 rectdiv * 5 / 2,
						 rectdiv * 6 / 2,
						 rectdiv * 7 / 2,
						 rectdiv * 8 / 2,
						 rectdiv * 9 / 2,
						 rectdiv * 10 / 2,
						 rectdiv * 11 / 2
						}
		                            };
		bg = &a;
		sfo.bgtemplate_O2 = bg;
		sfo.lowADC = 100;
		sfo.highADC = 1000;
		printf("*meed this or it's LOST*************deb7782-3*************************************** %i\n", sfo.priority);
//		sfo.bgtemplate_O2->loc__Ys[0] = sfo.indicators[0].loc__Y = sfo.disp__mainstat[0].locY + 150;
		//sfo.probestats = malloc(sizeof(unsigned short) * 9);
		system__O2probes = &sfo;








		// <><><><><>><><><><><><><><><><><><><><><><><><><><><><><><><><
    } // end construction block <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
		// <><><><><>><><><><><><><><><><><><><><><><><><><><><><><><><><
		printf("==deb7744===***WARN*** DO FOR ALL STRUCTS===============%i\n", system__O2probes->priority);
    int tempi = 0;
    struct fb_var_screeninfo orig_vinfo;
    long int screensize = 0;

    	time_t timer = time(NULL);
//    	time_t timer_start = time(NULL);
    	char* ts = (char*)ctime(&timer);





    // Open the framebuffer file for reading and writing
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
      printf("Error: cannot open framebuffer device.\n");
      return(1);
    }

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
      printf("Error reading variable information --1--.\n");
    }

    // Store for reset (copy vinfo to vinfo_orig)
    memcpy(&orig_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));


    // Change variable info
    vinfo.bits_per_pixel =  16;
	// uncomment to change the resolution
//    vinfo.xres = 2560;
//    vinfo.yres = 720; 
    vinfo.xres_virtual = vinfo.xres;
    // This line is causing a falure of the function call on Beaglebone Black
    // This could be an issue with memory size. Exclude *2 with BBB, and set page to 0 every time. It could flicker.
    vinfo.yres_virtual = vinfo.yres;// * 2;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
      printf("Error setting variable information --2-- .\n");
    }
    // hide cursor
    char *kbfds = "/dev/tty";
    int kbfd = open(kbfds, O_WRONLY);
    if (kbfd >= 0) {
        ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
    }
    else {
        printf("Could not open %s.\n", kbfds);
    }

    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
      printf("Error reading fixed information.\n");
    }

    page_size =  (finfo.line_length * vinfo.yres);

    // map fb to user mem
    screensize = finfo.smem_len;
    //printf("detected screen stats: line = %i, mem length = %i, yres = %i, screen size is %i, page size is %i \n", finfo.line_length, finfo.smem_len, vinfo.yres, screensize, page_size  );  //deb100
    //return /*deb222*/ -999;
    fbp = (char*)mmap(0,
              screensize,
              PROT_READ | PROT_WRITE,
              MAP_SHARED,
              fbfd,
              0);
    if ((int)fbp == -1) {
        printf("Failed to mmap\n");
    }
    else {
        // draw...
//-----------------------------------------------------------graphics loop here



    	int fps = 60;
    	int secs = 20;
    	int xloc = 1;
    	int yloc = 1;
 
    	int dispval = 1;


    	clear_screen(0);
    	draw_string(1500, 55, (char*)"CHARLIE ACTUAL MAIN SYSTEM", 26, 77, 0, 10, 2);

   	draw_rect(10, 10, finfo.line_length-10, 120, base_color, 5);
   	cur_page = 0;//(cur_page + 1) % 2;



    	draw_rect(finfo.line_length - 600, 600, 600, 100, base_color, 5);
	draw_string(finfo.line_length - 500, 630, (char*)"STATUS NOM", 10, base_color, 0, 10, 2);


	//deb100 set up io here.0-----------------------------
	//
	//
 	io_setup();	
	//
	//---------------------------------------------------
   
       unsigned short flagger = 0; 	
    for (int i = 1; i < (fps * secs); i++) {
	    // Use zero if all "one page" 
	// Time display-----------------
        timer = time(NULL);
	timer_global = timer;
        ts = (char*)ctime(&timer);
	timer_global = timer;	// update the global time used by the functions
	ts[5] -= 32;
	ts[6] -= 32;
	draw_string(100, 55, (char*)ts+4, 20, base_color, 0, 10, 2);
	// -----------------


	//deb9965
	//
	//short adc_0_keyboard;o
	if (flagger++ > 500) {
	flagger = 0;
	//deb9965
	//
	}



//	if (tempi++ >= 10){
//		system__trans->current_gear = (char)'D';  //deb100  shift
//		draw_string(900, 400, (char*)"HELLO WORLD", 11, 77, 0, 10, 1);
//	}  // if
	// this is a pre-check to see if there are critical top priority situations. 
	// If that is the case, then that attribute becomes sole display. 
	


	//if(system__trans->priority == 0)goto priority_TRANS;
	//if(system__cooling->priority == 0)goto priority_TEMP;
	//if(system__oil->priority == 0)goto priority_OIL;
	//if(system__fuel->priority == 0)goto priority_FUEL;
	//if(system__firecontrol->priority == 0)goto priority_FIRE;
	//if(system__O2probes->priority == 0)goto priority_O2;

	//printf("%i, %i, %i, %i, %i, %i\n", system__trans->priority,system__cooling->priority,system__oil->priority,system__fuel->priority,system__firecontrol->priority,system__O2probes->priority);

	if (clearflag){
	//if ((clearflag++ > 0) && (priorityflag == 0)){
		clear_ROI_primary();
   		fill_rect(finfo.line_length - 600, 600, 600, 100, 1);
   		draw_rect(finfo.line_length - 600, 600, 600, 100, base_color, 5);
		draw_string(finfo.line_length - 500, 630, (char*)"STATUS NOM", 10, base_color, 0, 10, 2);
		clearflag = 0;
	} // if




	// When a unit is set to priority 0, the basic display is skipped, status changed, and system focused. 
	// Some focus might have a solution via the keypad, or merely display the problem.
	//printf("deb44404 %i, %i, %i, %i, %i, %i\n", system__trans->priority, system__cooling->priority, system__oil->priority, system__fuel->priority, system__firecontrol->priority,  system__O2probes->priority);


	if (selectflag) goto select__JUMP;
	if (priorityflag) goto priority__JUMP;
//	if(system__trans->priority == 0)goto priority__JUMP;
//	if(system__cooling->priority == 0)goto priority__JUMP;
//	if(system__oil->priority == 0)goto priority__JUMP;
//	if(system__fuel->priority == 0)goto priority__JUMP;
//	if(system__firecontrol->priority == 0)goto priority__JUMP;
//	if(system__O2probes->priority == 0)goto priority__JUMP;
	//printf("deb23423 - reaching output routines\n");
		// Code if this code is hit then there is no priority display, status cubes and updates.
	syscheck__TRANS(system__trans, 1);
	syscheck__TEMP(system__cooling, 1);
	syscheck__OIL(system__oil, 1);
	syscheck__FUEL(system__fuel, 1);
	syscheck__FIREDETECT(system__firecontrol, 1);
	syscheck__O2(system__O2probes, 1);
	//clearflag--;
	goto JUMP__001;   // Skip priority displays
	// Each system check is made with status cube display flag OFF so that if this routine jumps into a priority display there won't 
	// be cube cluster interfering and the warning display system can focus.	
	// Some systems may have different needs 	





priority__JUMP:
if (selectflag == 0) {
	draw_rect(finfo.line_length - 600, 600, 600, 100, i, 5);
	draw_string(finfo.line_length - 500, 630, (char*)"STATUS ALERT", 12, i, 0, 10, 2);
}  // if

select__JUMP:



//syscheck__TRANS(system__trans, 0);
//syscheck__TEMP(system__cooling, 0);
//syscheck__OIL(system__oil, 0);
//syscheck__FUEL(system__fuel, 0);
//syscheck__FIREDETECT(system__firecontrol, 0);
//syscheck__O2(system__O2probes, 0);




// needs to be some means of using priority to control screen clearing when there is a priroty message otherwise the screen has a no-show zone where
// cubes used to be
//printf("deb7710 see note here\n"); 
// deb100 may need difference track: commanded or alerted?
// might want to pull an ROI call once?
if(system__firecontrol->priority == 0)goto priority_FIRE;
if(system__trans->priority == 0)goto priority_TRANS;
if(system__cooling->priority == 0)goto priority_TEMP;
if(system__oil->priority == 0)goto priority_OIL;
if(system__fuel->priority == 0)goto priority_FUEL;
if(system__O2probes->priority == 0)goto priority_O2;

priority_TRANS:	
	if (syscheck__TRANS(system__trans, 0)) {   // if there is trouble, the system check call itself returns 1
//		goto JUMP__001;
	} // if
	goto JUMP__001;
priority_TEMP:	
	if (syscheck__TEMP(system__cooling, 0)) {
		display_bar__vertical (&system__cooling->indicator,  system__cooling->temp__coolant_engine);
	} else {
		// deb 8871 this is causing flicker - might have to isolate the branches in focus again
		clear_ROI(2, 
			  200, 
			  finfo.line_length, // 2200, 
	  		  500, 
			  0);  
	}// if
	goto JUMP__001;
priority_OIL:	
	if (syscheck__OIL(system__oil, 0)) {
			// put code here to do emergency display
			// put code here to do display
//		goto JUMP__001;
	} // if
	goto JUMP__001;
priority_FUEL:	
	if (syscheck__FUEL(system__fuel, 0)) {
			// put code here to do emergency display
			// put code here to do display
//		goto JUMP__001;
	} // if
	goto JUMP__001;
priority_FIRE:	
	if (syscheck__FIREDETECT(system__firecontrol, 0)) {
		draw__ARMstat (400, 200, (char*) "FIRE", 4,  (char*)"WARNING ENGINE FIRE DETECTED", 28, system__firecontrol->disp__mainstat.buttonref, i);
	} else {
//		clear_ROI(2, 
//			  200, 
//			  finfo.line_length, // 2200, 
//	  		  500, 
//			  0);  
	}// if
	goto JUMP__001;
priority_O2:	
	// deb100 this could be interesting... 
	// Basically this is an alert rating meaning singling out some cylinder that has "fallen out" of the averages seen in the others. 
	if (syscheck__O2(system__O2probes, 0)) {
			// put code here to do emergency display
		for (unsigned char incr = 0; incr < 8; incr++) {
			// Note: if template guage setting min and max matches ADC output, then this interpolation is not necessary.
			bargraph__vertical(&system__O2probes->indicators[incr], system__O2probes->bgtemplate_O2, system__O2probes->probestats[incr]);
		} // for
	} // if
JUMP__001:


// scrap deb111
//	display_bar__vertical (tg, dispval++);
//	display_bar__horizontil (tg, dispval++);
	//draw_string(xloc, yloc, (char*)"HELLO WORLD", 11, 9, 0, 10, 1);
//	bargraph__vertical (tg, bg, dispval++);
//	if (dispval > tg->max) dispval = 1;
//	clear_ROI(100, 300, xloc + 222, 400, 0);
//	drawline(100,300, xloc++ + 222, 400, base_color);



        // switch page
        vinfo.yoffset = cur_page * vinfo.yres;
        ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo);
        // the call to waitforvsync should use a pointer to a variable
        // https://www.raspberrypi.org/forums/viewtopic.php?f=67&t=19073&p=887711#p885821
        // so should be in fact like this:
        __u32 dummy = 0;
        ioctl(fbfd, FBIO_WAITFORVSYNC, &dummy);
	if (yloc >= vinfo.yres/2) yloc = 1;
	if (xloc >= 100) yloc = 1;




	//printf("deb777-777-777 need to do the ADC inputs. Note: use one button to display all of O2 systems to simplify. Need sensor inputs too\n");


	unsigned short adc_val = read__ADC(0);  // first ADC port is keyboard input (actually an all analog 16 key pad)
	printf("ADC VALUE = %i\n", adc_val);
	//if (adc_val > 4000) break;	// deb555 do more with this... danger danger danger.
	if (adc_val > 4000) {
		break;		// deb something else please
	}  // deb555 do more with this... danger danger danger.
	if ((adc_val < 3800) && (adc_val > 3700)){   // RESET
		system__firecontrol->select = 0;
		system__trans->select = 0;
		system__cooling->select = 0;
		system__oil->select = 0;
		system__fuel->select = 0;
		system__O2probes->select = 0;
		selectflag = 0;	
	} // if
	if ((adc_val > 2000) && (adc_val < 2200)){
	       	system__O2probes->select = 1 ;
	       	system__O2probes->priority = 0;
		selectflag = 1;
 	}  // if

	

    }  // for









//-----------------------------------------------------------graphics loop here END



    	} // if

  //  time_t time_end = time(NULL);
    // unmap fb file from memory
    	munmap(fbp, screensize);
    // reset cursor
    	if (kbfd >= 0) {
        	ioctl(kbfd, KDSETMODE, KD_TEXT);
        	// close kb file
        	close(kbfd);
    	}
    	// reset the display mode
    	if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &orig_vinfo)) {
        	printf("Error re-setting variable information.\n");
    	}
    	// close fb file    
    	close(fbfd);








    return 0;

}


/** notes 05-04-2020
 * Tip: are Bluecat fenders painted?
 * Notes
 * Next up, some emergency display for Fire Control
 * suggestions: a three box display for the three zones
 * a display of the keypad with one solid button for which button to press to initiate 
 * extinguisher action :)






	4095	3730	3415	3152
	2727	2559	2480	2277
	2044	1945	1862	1793
 	1627	1307	1094	940




























 */





