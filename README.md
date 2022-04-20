# OPL3USB

OPL3USB is based on:
  1. https://github.com/denjhang/y262lpt-v0.3b which is a clone of https://www.serdashop.com/OPL3LPT
  2. OPL3 Duo! Board https://github.com/DhrBaksteen/ArduinoOPL2

OPL3USB is a board plug into usb works as a serial port (Arduino) and use a OPL3LPT (modified) as an OPL3 sound card for Dosbox-X.

*modified OPL3LPT*
means 3 lines added :
  1. DB25-13--------VCC---------5V   ( if your opl3lpt makes noise , use the original power line instead )
  2. DB25-14--------IC----------RST
  3. DB25-15--------RD----------HC12
   
  
