# Characters_To_Linux_Buffer_THE_HARD_WAY
Quick and dirty characters to Linux Buffer in C - the hard way - with "glyph arrays", array fu, pointer fu, but no breasts, explosions,  or car chases
This project is all about character. When looking to put characters to screen for a Linux Buffer, I wanted to create a display for data but the actual text of the console was too small on the barbones non-GUI Raspberry Pi that was targted for this project. 
This project is based on other work, a good example of framebuffer handling in Linux. See 
 http://raspberrycompote.blogspot.com/2015/01/low-level-graphics-on-raspberry-pi-part.html
 So what was done in for this project was a bunch of arrays of 1s and 0s representing characters and numbers were created and these arrays are looped through with a "set pixel" routine to determine when and where an actual pixel is written to with a color value. The fonts are based on the Aliens sentry guns. https://www.youtube.com/watch?v=HQDy-5IQvuU
 This demo assumes that not every system is going to be fast and have an HDMI screen (that overheats in my case - don't be me) so pay attention to this line:
 
 vinfo.bits_per_pixel =  8;
 
 The color values range 0 to 15. This is not the deepest color of course. If you raise this "depth" you could slow things down, but you get a wider range of colors. 
 And these:
 
 vinfo.xres = 1184;
 
 vinfo.yres = 624; 
 
 Your screen, be it HDMI, or some TFT LCD perhaps - it matters not to a Linux Framebuffer routine as long as you get the /dev/fbN right, where N is the Nth number of displays - is apt to differ. So dimensions of the screen and "depth" could differ. I have a TFT LCD screen that handles the output from an old noir infrared Raspberry camera well, but try any x32 bitmap on it without some conversion and things get interesting. 
 My goal overall was "just get some text to the screen". Arrays were created using something close to "Aliens auto-gun font" and routines written to set pixels to a given color value based on where we want it, X and Y and all that, and what's 1 or 0 in the glyph array. 
 
 From there some other routines were spawned: printing a string. Printing a number display. And rectangles, filled or hollow with a border, and lines. 
 
 This demo shows a little bit of animation, but it's not the goal. Using a kind of "buffer flipping" approach, which is  how most graphics work - writing to a memory space, like an array first, that equals the display in size and color depth, with color values, and then "blitting" (an old word from the 1990s probably) that memory to the same memory that is mapped to the screen. This makes for very smooth transition from one screen to the other. 
 
 So animation is possible, but not the goal. It's simple enough to just to everything to that "back buffer" and then write it to the screen buffer. Once you have this kind of code down, such as the code from Raspberry Compote, what you do to that back buffer is your business: from our humble text to 3D rendering. 
 
 This demo assumes that older or less capable systems might be in use. So all of the characters are in a block. The actual glyphs - represented by arrays are "held" via a "pointer to array" array. In the block, this array is assigned pointers to the array data, so once the block is passed by, those objects are gone. This lets you pick and choose what characters you use, and discard the rest. 
 Also to note, there are two arrays representing two font sizes. Each are 128 elements, and the nth element corresponds with the unsigned int value of the character. So for that reason, a  SPACE is in element 32. The ASCII value of "S" is 83, so the 83rd elements of the arrays is a pointer to the array of S. 
 
Smaller numbers arrays are also used to be elements that point to arrays representing numbers, the Nth element corresponding to the number, ranging from 0-9. This spares us the atoi call that would be used for letters. 

Overall, tested on a 1st gen single core Raspberry, it was fairly fast. Results may vary. It's basic C code all the way too. 

There are other ways to to characters onto a screen. But I opted for this quick and dirty measure to ensure flexibility and simplicity. The "array fu" of this code may also prove useful for other things. The array need not be a letter or number or glyph of any sort - it can be a sprite for example, or anything you want. You are limited by the screen you use and the processor. 

If you want more fancy fonts, there are demos on the internet that take "real" fonts and convert them to pixels. If you can get these into arrays, such as a program tha reads a bitmap and turns it into an array of color values, you might use them as fonts in this program. Keep in mind that all of these, while displayed in code as 2D arrays, are really 1D arrays. So you have to know ahead of time what the dimensions of your arrays are supposed to be. The arrays in this demo were created manually. The entire ASCII ranges - 128 characters, are not completed. So if you want something that is not there, like lower case characters and punctuation, you will have to create it yourself. 

Have fun with this code. 
