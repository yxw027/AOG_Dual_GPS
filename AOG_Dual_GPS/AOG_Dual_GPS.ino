/*

ESP32 programm for UBLOX receivers to send NMEA to AgOpenGPS or other program

works with 1 or 2 receivers

1 receiver to send position from UBXPVT message to ESP32
2 receivers to get position, roll and heading from UBXPVT + UBXRelPosNED via UART to ESP32

ESP sending $PAOGI or $GGA+VTG+HDT sentence via UDP to IP x.x.x.255 at port 9999 or via USB

AgOpenGPS sending NTRIP via UDP to port 2233(or USB) -> ESP sends it to UBLOX via UART

filters roll, heading and on weak GPS signal, position with filter parameters changing dynamic on GPS signal quality

by Matthias Hammer (MTZ8302) 26. Jan 2020

*/
//to do: Ethernet integration
//to think about: will also work with 2 receivers sending GGA, so not only fo UBlox, code is in "EEPROM"


#define useWiFi 1 //0 disables WiFi = use USB so no debugmessages!! might work on Ardiuno mega not tested
struct set {
    // IO pins ----------------------------------------------------------------------------------------
    byte RX0 = 3;//USB - change only if not USB!
    byte TX0 = 1;//USB - change only if not USB!

    //connection plan:
    // ESP32---- 1.F9P GPS pos ----- 2.F9P Heading-----Sentences
    //  RX1---------TX1--------------------------------UBX-Nav-PVT out   (=position+speed)
    //  TX1---------RX1--------------------------------RTCM in         (NTRIP comming from AOG to get absolute/correct postion
    //  RX2-------------------------------TX1----------UBX-RelPosNED out (=position relative to other Antenna)
    //  TX2-------------------------------RX1----------
    //              RX2-------------------TX2----------RTCM 1077+1087+1097+1127+1230+4072.0+4072.1 (activate in both F9P!! = NTRIP for relative positioning)

    byte RX1 = 27;              //1. F9P TX1 GPS pos
    byte TX1 = 16;              //1. F9P RX1 GPS pos

    byte RX2 = 25;              //2. F9P TX1 Heading
    byte TX2 = 17;              //2. F9P RX1 Heading

    byte LED_PIN_WIFI = 2;      // WiFi Status LED 0 = off
    byte WIFI_LED_ON = HIGH;    //HIGH = LED on high, LOW = LED on low

    byte AOGNtrip = 1;          // AOG NTRIP client 0:off 1:on listens to UDP port or USB serial
    byte sendOGI = 1;           //1: send NMEA message 0: off
    byte sendVTG = 0;           //1: send NMEA message 0: off
    byte sendGGA = 0;           //1: send NMEA message 0: off
    byte sendHDT = 0;           //1: send NMEA message 0: off

    byte GPSPosCorrByRoll = 1;      // 0 = off, 1 = correction of position by roll (AntHight must be > 0)
    double rollAngleCorrection = 0.0;

    double headingAngleCorrection = 90;
    double AntDist = 74.0;          //cm distance between Antennas
    double AntHight = 228.0;        //cm hight of Antenna
    double virtAntRight = 37.0;     //cm to move virtual Antenna to the right
    double virtAntForew = 0.0;      //cm to move virtual Antenna foreward

    double AntDistDeviationFactor = 1.2; // factor (>1), of whom lenght vector from both GPS units can max differ from AntDist before stop heading calc
    byte filterGPSposOnWeakSignal = 1;    //filter GPS Position on weak GPS signal
    
    byte DataTransVia = 1;          //transfer data via 0: USB 1: WiFi
#if useWiFi
    //WiFi---------------------------------------------------------------------------------------------
    //tractors WiFi
    char ssid[24] = "Fendt_209V";           // WiFi network Client name
    char password[24] = "";                 // WiFi network password//Accesspoint name and password

    const char* ssid_ap = "GPS_unit_ESP_Net";// name of Access point, if no WiFi found
    const char* password_ap = "";
    unsigned long timeoutRouter = 65;       //time (s) to search for existing WiFi, than starting Accesspoint 

    //static IP
    byte myIP[4] = { 192, 168, 1, 79 };     // Roofcontrol module 
    byte gwIP[4] = { 192, 168, 1, 1 };      // Gateway IP also used if Accesspoint created
    byte mask[4] = { 255, 255, 255, 0 };
    byte myDNS[4] = { 8, 8, 8, 8 };         //optional

    unsigned int portMy = 5544;             //this is port of this module: Autosteer = 5577 IMU = 5566 GPS = 
    unsigned int portAOG = 8888;            // port to listen for AOG
    unsigned int AOGNtripPort = 2233;       //port NTRIP data from AOG comes in
    unsigned int portDestination = 9999;    // Port of AOG that listens
#endif
}; set GPSSet;

bool debugmode = false;
bool debugmodeUBX = false;
bool debugmodeHeading = false;
bool debugmodeVirtAnt = false;
bool debugmodeFilterPos = false;
bool EEPROM_clear = false;


// WiFistatus LED 
// blink times: searching WIFI: blinking 4x faster; connected: blinking as times set; data available: light on; no data for 2 seconds: blinking
unsigned int LED_WIFI_time = 0;
unsigned int LED_WIFI_pulse = 700;   //light on in ms 
unsigned int LED_WIFI_pause = 700;   //light off in ms
boolean LED_WIFI_ON = false;
unsigned long Ntrip_data_time = 0;




//Kalman filter roll
double rollK, rollPc, rollG, rollXp, rollZp, rollXe;
double rollP = 1.0;
double rollVar = 0.1; // variance, smaller: faster, less filtering
double rollVarProcess = 0.3;// 0.0005;// 0.0003;  bigger: faster, less filtering// replaced by fast/slow depending on GPS quality
//Kalman filter heading
double headK, headPc, headG, headXp, headZp, headXe;
double headP = 1.0;
double headVar = 0.1; // variance, smaller, more faster filtering
double headVarProcess = 0.1;// 0.001;//  bigger: faster, less filtering// replaced by fast/slow depending on GPS quality
//Kalman filter lat
double latK, latPc, latG, latXp, latZp, latXe;
double latP = 1.0;
double latVar = 0.1; // variance, smaller, more faster filtering
double latVarProcess = 0.3;//  replaced by fast/slow depending on GPS qaulity
//Kalman filter lon
double lonK, lonPc, lonG, lonXp, lonZp, lonXe;
double lonP = 1.0;
double lonVar = 0.1; // variance, smaller, more faster filtering
double lonVarProcess = 0.3;// replaced by fast/slow depending on GPS quality

double VarProcessVeryFast = 0.6;//  used, when GPS signal is weak, no roll, but heading OK
double VarProcessFast = 0.3;//  used, when GPS signal is weak, no roll, but heading OK
double VarProcessMedi = 0.2;//  used, when GPS signal is  weak, no roll no heading
double VarProcessSlow = 0.1;//  used, when GPS signal is  weak, no roll no heading
double VarProcessVerySlow = 0.03;//  used, when GPS signal is  weak, no roll no heading
bool filterGPSpos = false;



#if useWiFi
//WIFI
IPAddress IPToAOG(192, 168, 1, 255);//IP address to send UDP data to
byte myIPEnding = 79;             //ending of IP adress x.x.x.79 
byte my_WiFi_Mode = 0;  // WIFI_STA = 1 = Workstation  WIFI_AP = 2  = Accesspoint
#endif

//UBX
byte UBXRingCount1 = 0, UBXDigit1 = 0, UBXDigit2 = 0, OGIfromUBX = 0;
short UBXLenght1 = 100, UBXLenght2 = 100;
constexpr unsigned char UBX_HEADER[] = { 0xB5, 0x62 };//all UBX start with this
bool isUBXPVT1 = false,  isUBXRelPosNED = false, existsUBXRelPosNED = false;

unsigned long NtripDataTime = 0, now = 0;
double virtLat = 0.0, virtLon = 0.0;//virtual Antenna Position

//NMEA
byte OGIBuffer[90], HDTBuffer[20], VTGBuffer[50], GGABuffer[80];
bool newOGI = false, newHDT = false, newGGA = false, newVTG = false;
byte OGIdigit = 0, GGAdigit = 0, VTGdigit = 0, HDTdigit = 0;

//heading + roll
constexpr byte GPSHeadingArraySize = 3;
double GPSHeading[GPSHeadingArraySize];
byte headRingCount = 0, noRollCount = 0, noHeadingCount = 0;
constexpr double PI180 = PI / 180;
bool dualGPSHeadingPresent = false, rollPresent = false, virtAntPosPresent = false;
double roll = 0.0;
//byte rollRingCont = 0;
byte dualAntNoValue = 0, dualAntNoValueMax = 6;// if dual Ant value not valid for xx times, send position without correction/heading/roll


// Variables ------------------------------
struct NAV_PVT {
    unsigned char cls;
    unsigned char id;
    unsigned short len;
    unsigned long iTOW;  //GPS time ms
    unsigned short year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char min;
    unsigned char sec;
    unsigned char valid;
    unsigned long tAcc;
    long nano;
    unsigned char fixType;//0 no fix....
    unsigned char flags;
    unsigned char flags2;
    unsigned char numSV; //number of sats
    long lon;   //deg * 10^-7
    long lat;   //deg * 10^-7
    long height;
    long hMSL;  //heigt above mean sea level mm
    unsigned long hAcc;
    unsigned long vAcc;
    long velN;
    long velE;
    long velD;
    long gSpeed; //Ground Speed mm/s
    long headMot;
    unsigned long sAcc;
    unsigned long headAcc;
    unsigned short pDOP;
    unsigned char flags3;
    unsigned char reserved1;
    long headVeh;
    long magDec;//doesnt fit, checksum was 4 bytes later, so changes from short to long
    unsigned long magAcc;
    unsigned char CK0;
    unsigned char CK1;
};
constexpr byte sizeOfUBXArray = 3;
NAV_PVT UBXPVT1[sizeOfUBXArray];


struct NAV_RELPOSNED {
    unsigned char cls;
    unsigned char id;
    unsigned short len;
    unsigned char ver;
    unsigned char res1;
    unsigned short refStID;
    unsigned long iTOW;
    long relPosN;
    long relPosE;
    long relPosD;
    long relPosLength;
    long relPosHeading;
    unsigned char res2;
    char relPosHPN;
    char relPosHPE;
    char relPosHPD;
    char relPosHPLength;
    unsigned long accN;
    unsigned long accE;
    unsigned long accD;
    unsigned long accLength;
    unsigned long accHeading;
    unsigned char res3;
    unsigned long flags;
    unsigned char CK0;
    unsigned char CK1;
};
NAV_RELPOSNED UBXRelPosNED;

#if useWiFi
#include <AsyncUDP.h>
#include <WiFiSTA.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WiFi.h>
#include <EEPROM.h>

//instances----------------------------------------------------------------------------------------
AsyncUDP udpRoof;
AsyncUDP udpNtrip;
WiFiServer server(80);
WiFiClient client_page;
#endif

void setup()
{
    delay(200);
    delay(200);
    delay(50);
	//start serials
    Serial.begin(115200);
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, GPSSet.RX1, GPSSet.TX1);
    delay(10);
    Serial2.begin(115200, SERIAL_8N1, GPSSet.RX2, GPSSet.TX2);
    delay(10);

    Serial.println();//new line

    if (GPSSet.LED_PIN_WIFI != 0) { pinMode(GPSSet.LED_PIN_WIFI, OUTPUT); }

#if !useWiFi
    GPSSet.DataTransVia = 0;//set data via USB
#endif
    

#if useWiFi
    restoreEEprom();
    delay(10);

    //start WiFi
    WiFi_Start_STA();
    if (my_WiFi_Mode == 0) {// if failed start AP
        WiFi_Start_AP(); 
        delay(100); 
    }
    delay(200);

    //init UPD listening to AOG
	if (udpNtrip.listen(GPSSet.AOGNtripPort))
	{
		Serial.print("NTRIP UDP Listening to port: ");
		Serial.println(GPSSet.AOGNtripPort);
		Serial.println();
	}
    delay(50);
    if (udpRoof.listen(GPSSet.portMy))
    {
        Serial.print("UDP writing to IP: ");
        Serial.println(IPToAOG); 
        Serial.print("UDP writing to port: ");
        Serial.println(GPSSet.portDestination);
        Serial.print("UDP writing from port: ");
        Serial.println(GPSSet.portMy);
    }
    delay(100);
    server.begin();
#endif
}

void loop()
{
	getUBX();//read serials    

	if (UBXRingCount1 != OGIfromUBX)//new UXB exists
	{Serial.println("new UBX to process");
		headingRollCalc();
		if (dualGPSHeadingPresent) {
			//virtual Antenna point?
			if ((GPSSet.virtAntForew != 0) || (GPSSet.virtAntRight != 0) ||
				((GPSSet.GPSPosCorrByRoll == 1) && (GPSSet.AntHight > 0)))
			{//all data there
				virtualAntennaPoint();
			}

      filterPosition();//runs allways to fill kalman variables, decide in NMEA buildXXX if filtered position is used

			if (GPSSet.sendGGA) { buildGGA(); }
			if (GPSSet.sendVTG) { buildVTG(); }
			if (GPSSet.sendHDT) { buildHDT(); }
			buildOGI();//should be build anyway, to decide if new data came in
		}
		else //only 1 Antenna: this way
		{
			virtAntPosPresent = false;
			dualAntNoValue++;//watchdog
				//filter position: set kalman variables
				//0: no fix 1: GPS only -> filter slow, else filter fast, but filter due to no roll compensation
			if (UBXPVT1[UBXRingCount1].fixType <= 1) { latVarProcess = VarProcessSlow; lonVarProcess = VarProcessSlow; }
			else { latVarProcess = VarProcessMedi; lonVarProcess = VarProcessMedi; }

			filterPosition();
      filterGPSpos = true;
  
			if (dualAntNoValue > dualAntNoValueMax) {//no new values exist, so send only pos
				if ((debugmodeHeading) || (debugmodeVirtAnt)) { Serial.println("no dual Antenna values, no heading/roll, watchdog: send only Pos"); }
				if (GPSSet.sendGGA) { buildGGA(); }
				if (GPSSet.sendVTG) { buildVTG(); }
				if (GPSSet.sendHDT) { buildHDT(); }
				buildOGI();//should be build anyway, to decide if new data came in

				dualAntNoValue = dualAntNoValueMax;//prevent overflow
			}
		}
	}

	if (GPSSet.DataTransVia == 0) {//use USB
		if (GPSSet.AOGNtrip == 1) { doSerialNTRIP(); } //gets USB NTRIP and sends to serial 1   
        if ((newOGI) && (GPSSet.sendOGI == 1)) {
			for (byte n = 0; n < (OGIdigit - 1); n++) { Serial.write(OGIBuffer[n]); }
			Serial.println();
			newOGI = false;
		}
		if (newVTG) {
			for (byte n = 0; n < (VTGdigit - 1); n++) { Serial.write(VTGBuffer[n]); }
			Serial.println();
			newVTG = false;
		}
		if (newHDT) {
			for (byte n = 0; n < (HDTdigit - 1); n++) { Serial.write(HDTBuffer[n]); }
			Serial.println();
			newHDT = false;
		}
		if (newGGA) {
			for (byte n = 0; n < (GGAdigit - 1); n++) { Serial.write(GGABuffer[n]); }
			Serial.println();
			newGGA = false;
		}
	}

#if useWiFi

	if (GPSSet.DataTransVia == 1) {//use WiFi
		if (GPSSet.AOGNtrip == 1) { doUDPNtrip(); } //gets UDP NTRIP and sends to serial 1 

		if ((newOGI) && (GPSSet.sendOGI == 1)) {
			udpRoof.writeTo(OGIBuffer, OGIdigit, IPToAOG, GPSSet.portDestination);
			newOGI = false;
		}
		if (newGGA) {
			udpRoof.writeTo(GGABuffer, GGAdigit, IPToAOG, GPSSet.portDestination);
			newGGA = false;
		}
		if (newVTG) {
			udpRoof.writeTo(VTGBuffer, VTGdigit, IPToAOG, GPSSet.portDestination);
			newVTG = false;
		}
		if (newHDT) {
			udpRoof.writeTo(HDTBuffer, HDTdigit, IPToAOG, GPSSet.portDestination);
			newHDT = false;
		}
	}

	doWebInterface();

#endif
    
}
