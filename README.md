# AOG_Dual_GPS
Bridge between GPS receiver and AgOpenGPS: transmit GPS Signal via WiFi or USB.

Works with 2 UBlox to calculate heading+roll. Corrects position by using roll and filters, depends on GPS signal quality.
With 1 receiver: filters and transmits position to AOG and NTRIP from AOG to UBlox

for ESP32

you can use WiFi connection or USB

should also run on Arduino (untested)

config and wireing of the receiver boards:
Connect Tx2 and Rx2 from one board to the other to get RTCM over (1x).
First set on both boards UART1 speed to 115200, UART2 speed to 460800 and activate NMEA+UBX+RTCM3 to inputs and UBX +RTCM3 to output on UART1+2. Then set Nav Rate to 100ms (10Hz).
In the messages I deactivated everything expect of (UBX) NavPVT on UART1 at one board and NavRelPosNED on UART1 at the other board. At both activate the RTCM messages 1077/1087/1097/1127/1230/4072.0/4072.1 on UART2.

Don’t forget to save the config!

The ESP32 is connected to both boards UART1 separately (2x2 pins, RX/TX crossed (setup zone in code)). From one board you get UBX NavPVT and from the other UBX NavRelPosNED.

Also see UBlox PDFs

!!When using a new Version, set EEPROM_clear = true; (line 93) - flash - boot reset to EEPROM_clear = false; and flash again to reset settings!!
