# Unload

This is unload utility for DOS drivers USBASPI.SYS & DI1000DD.SYS  
They are used to access USB flash drives from DOS.  
But once they are loaded, you can't change USB flash drive.  
When you remove USB flash drive from USB slot, you lose access even you insert the same USB flash back.

How to use this utility (UL.EXE)  
- insert USB flash drive(s) in USB port;  
- load drivers manualy:  
    `devload /h usbaspi.sys /e`  
    `devload /h di1000dd.sys`  
  or with -l switch (DEVLOAD.COM required anyway):  
    `ul -l`  
- now you have access to USB flash drive(s);  
- unload drivers:  
    `ul`  
- now you can change USB flash drive and load drivers again  

If drivers were loaded by CONFIG.SYS this utility wan't find they.
Please don't use SMARTDRV.EXE.

This unload utility completely removes USBASPI.SYS & DI1000DD.SYS from DOS conventional memory.  
Also removes allocated XMS handles, drive letters  
and modifies drives quantity in DOS List of Lists (+20h) and in BIOS data area (+75h).  

Tested with USBASPI versions 2.20, 2.24, 2.27 (Panasonic) and 2.27x (YAYA DIY 2010/09/16) with /e option (EHCI mode);
Free DOS, DEVLOAD v3.25, JEMMEX v5.78 (now not neccessary).
