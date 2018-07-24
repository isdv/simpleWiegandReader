# simpleWiegandReader
Wiegand Reader -  Linux kernel module for read Em Marine card. 


Uses Linux GPIO interface in device-independent way. Read card num from sysfs.
Support Wiegand 26 only. Work on Linux kernel <=4.14.


Set correct values in "wiegand_reader.c":

    // Wiegand D0 Pin
    #define D0_GPIO 1 
    // Wiegand D1 Pin 
    #define D1_GPIO 0 


Compile:
    
    make
    
    insmod wiegand_reader.ko
    

Test:

    python3 read.py
    
  
  
Tested on OrangePI PC.


