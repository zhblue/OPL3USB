# OPL3USB

OPL3USB is based on:
  1. https://github.com/zhblue/y262lpt-v0.3b which is a clone of https://www.serdashop.com/OPL3LPT
  2. OPL3 Duo! Board https://github.com/DhrBaksteen/ArduinoOPL2

OPL3USB is a board plug into usb works as a serial port (Arduino) and use a OPL3LPT (modified) as an OPL3 sound card for [Dosbox-X](https://dosbox-x.com/).

This project can be a low cost replacement to [SudoMaker's RetroWave](https://github.com/SudoMaker/RetroWave) if you already have an Arduino-nano or Arduino-UNO.

*modified OPL3LPT*
means 3 lines added :
  1. DB25-13--------VCC---------5V      ( if your opl3lpt makes noise , use the original power line instead )
  2. DB25-14--------IC----------YMF262-P3/YM3812-P3
  3. DB25-15--------RD----------YMF262-P7/YM3812-P6
   
***the clone verion above has already include these modifications.***

Created using free version of LCEDA https://lceda.cn/
   
![模拟图](https://github.com/zhblue/OPL3USB/blob/main/OPL3USB.png?raw=true)
