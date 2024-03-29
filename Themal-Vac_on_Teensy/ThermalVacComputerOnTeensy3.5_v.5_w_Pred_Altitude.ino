/*
  /-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\
  \               Thermal-Vac Computer on Teensy 3.5                            /
  /               To be Used with the PuTTY software                            \
  \               Written by Billy Straub (strau257) Summer 2019                /
  /               Based on Code by Jacob Meyer (meye2497)                       \
  \-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/-\-/


   Teensy 3.5 pin connections:
   --------------------------------------------------------------------------------------------------------------------------------------------------
     Component                    | Pins used               | Notes
     -                              -                         -
     SD card reader               |                         |  SD card reader built into teensy 3.5 board
     xBee serial                  | D33 D34                 |  First pin is D_in and second is D_out
     Honeywell Pressure Sensor #1 | A0                      |  
     Honeywell Pressure Sensor #2 | A1                      |  
     Adafruit Thermocouple #1     | D9  D8  D7  D5          |  First SCK, then SDO, then SDI, then CS pins in that order
     Adafruit Thermocouple #2     | D9  D8  D7  D6          |  Pins in same order as above (Note: different chip select (CS) for second thermocouple)
     EC2-5TNU Relay #1            | D20 D19                 |  First is Reset pin, second is Set Switch pin
     EC2-5TNU Relay #2            | D18 D17                 |  Pins in same order as above
   --------------------------------------------------------------------------------------------------------------------------------------------------

  COMMANDS:
    If "R1.OFF" is typed into the monitor, relay #1 turns off
    If "R1..ON" is typed into the monitor, relay #1 turns on
    If "R2.OFF" is typed into the monitor, relay #2 turns off
    If "R2..ON" is typed into the monitor, relay #2 turns on  
*/

//Libraries
#include <Adafruit_MAX31856.h>  //Thermocouple library
#include <XBee.h>  //XBee library
#include <SD.h>  //SD card library
#include <math.h>  //Math library allowing for advanced formulas



//Pin Definitions
#define SDchipSelect BUILTIN_SDCARD   //SD Pin Definition
#define XBee_Serial Serial5           //XBee Hardware Serial Communication Pins Definition
#define switch_pin 18                 //Relay #1 Set Pin Definition
#define reset_pin 17                  //Relay #1 Reset Pin Definition
#define switch_pin_2 20               //Relay #2 Set Pin Definition
#define reset_pin_2 19                //Relay #2 Reset Pin Definition
#define Honeywell_1 A0                //Honeywell Pressure Sesnor #1 Pin Definition
#define Honeywell_2 A1                //Honeywell Pressure Sesnor #2 Pin Definition


//Thermocouple Object and Pin Definitions
//Template: Adafruit_MAX31856 maxthermo# = Adafruit_MAX31856(cs)
Adafruit_MAX31856 maxthermo1 = Adafruit_MAX31856(5, 7, 8, 9);
Adafruit_MAX31856 maxthermo2 = Adafruit_MAX31856(6, 7, 8, 9);


//SD File Logging
File datalog;                     //File Object for datalogging
char filename[] = "TVac00.csv";   //Template for file name to save data
bool SDactive = false;            //Used to check for SD card before attempting to log data


XBee xBee = XBee(&XBee_Serial);  //XBee Object
String networkID = "CAFE";       //XBee Network ID - Choose a unique 2 to 4 digit, A-F code (also, later used to set xBee Pan ID)


//Honeywell Pressure Sensors #1 and #2 Data Types
int pressureSensor1;
float pressureSensorVoltage1;
float PressurePSI1;
float PressureATM1;
int pressureSensor2;
float pressureSensorVoltage2;
float PressurePSI2;
float PressureATM2;
float PressureATMavg;

//Relay Status Data Type (starts relay in "off mode" on startup)
boolean switchStatus = false;
boolean resetStatus = true;
boolean switchStatus2 = false;
boolean resetStatus2 = true;

//Predicted Altitude (Based on Pressure) Data Type
float AltitudeM;

//Command and Time Data Type
String inputPressure1;          //inputPressure 1 through 6 holds the recieved command/data from the xBee monitor
String inputPressure2;
String inputPressure3;
String inputPressure4;
String inputPressure5;
String inputPressure6;
unsigned long time;              //Data type used for time variable



void setup() {

  Serial.begin(9600);  //Begin Serial Monitor

    XBee_Serial.begin(9600);  //Begin XBee Communications
  //Set XBee send/recieve channels (commented out part would print the xBee's response along with changing the settings)
 /* Serial.println(xBee.enterATmode());
    Serial.println(xBee.atCommand("ATMY0"));
    Serial.println(xBee.atCommand("ATDL1"));
    Serial.println(xBee.atCommand("ATID" + networkID));
    Serial.println(xBee.exitATmode()); */
    xBee.enterATmode();                   //Enter command mode
    xBee.atCommand("ATID" + networkID);   //Set xBee's Pan ID
    xBee.atCommand("ATMY0");              //Set recieving channel to 1
    xBee.atCommand("ATDL1");              //Set sending channel to 0
    xBee.atCommand("ATDH0");              //Set high recieving channel to 0, but not often used
    xBee.atCommand("ATWR");               //Save configuration changes
    xBee.exitATmode();                    //Exit command mode


  //Setup Adafruit MAX_31856 Thermocouples #1 and #2
  maxthermo1.begin();
  maxthermo2.begin();
  maxthermo1.setThermocoupleType(MAX31856_TCTYPE_K);
  maxthermo2.setThermocoupleType(MAX31856_TCTYPE_K);

  
    //SD Card Setup
    pinMode(10, OUTPUT);                                      //Needed for SD library, regardless of shield used
    pinMode(SDchipSelect, OUTPUT);
    Serial.print("Initializing SD card...");
    xBee.print("Initializing SD card...");
    if (!SD.begin(SDchipSelect)){                              //Attempt to start SD communication
      Serial.println("Card not inserted or the card is broken.");          //Print out error if failed; remind user to check card
      xBee.println("Card not inserted or the card is broken.");
    }
    else {                                                    //If successful, attempt to create file
      Serial.println("Card initialized successfully.\nCreating File...");
      xBee.println("Card initialized successfully.\nCreating File...");
      for (byte i = 0; i < 100; i++) {                        //Can create up to 100 files with similar names, but numbered differently
        filename[4] = '0' + i / 10;
        filename[5] = '0' + i % 10;
        if (!SD.exists(filename)) {                           //If a given filename doesn't exist, it's available
          datalog = SD.open(filename, FILE_WRITE);            //Create file with that name
          SDactive = true;                                    //Activate SD logging since file creation was successful
          Serial.println("Logging to: " + String(filename));  //Tell user which file contains the data for this run of the program
          xBee.println("Logging to: " + String(filename));
          break;                                              //Exit the for loop now that we have a file
        }
      }
      if (!SDactive) {
      Serial.println("No available file names; clear SD card to enable logging");
      xBee.println("No available file names; clear SD card to enable logging");
      } 
    }

  //First part of header giving commands
  Serial.println("--- Command list: ---");
  Serial.println("R1..ON -> Turn First Relay ON");  
  Serial.println("R1.OFF -> Turn First Relay OFF");  
  Serial.println("R2..ON -> Turn Second Relay ON");
  Serial.println("R2.OFF -> Turn Second Relay OFF");  
  xBee.println("--- Command list: ---");
  xBee.println("R1..ON -> Turn First Relay ON");  
  xBee.println("R1.OFF -> Turn First Relay OFF");  
  xBee.println("R2..ON -> Turn Second Relay ON");
  xBee.println("R2.OFF -> Turn Second Relay OFF");  

  delay(500);

  //Second part of header leading data columns
  String headerPRINT = "Pres 1 (atm)    Pres 2 (atm)    Temp 1 (C)    Temp 2 (C)    Time (s)    Input-Pres (mBar)    Pred-Alt (m)";  
  String headerLOG = "Pres 1 (atm),Pres 2 (atm),Temp 1 (C),Temp 2 (C),Time (s),Input-Pres (mBar),Pred-Alt (m)";
  Serial.println(headerPRINT);
  xBee.println(headerPRINT);
  if (SDactive) {
    datalog.println(headerLOG);
    datalog.close();
  }


  //Relay Setup
  pinMode(reset_pin, OUTPUT);
  pinMode(switch_pin, OUTPUT);
  SWITCH();
  pinMode(reset_pin_2, OUTPUT);
  pinMode(switch_pin_2, OUTPUT);
  SWITCH2();


  Serial.flush();       //Clear Receive Buffer


  delay(500);
  
}



void loop() {

  //Relay Control and External Pressure Input Void (because the code is too long to include in the loop and keep the loop being navigatable)
  RECEIVED_COMMAND();

  //Honeywell Pressure Sensors #1 and #2
  pressureSensor1 = analogRead(Honeywell_1);                              //Reads the Honeywell Pressure Sensor's Analog Pin
  pressureSensorVoltage1 = pressureSensor1 * (5.0 / 1024);                //Converts the Digital Number to Voltage
  PressurePSI1 = (pressureSensorVoltage1 - (0.1 * 5.0)) / (4.0 / 15.0);   //Converts the Voltage to Pressure in PSI
  PressureATM1 = PressurePSI1 / 14.696;                                   //Converts Pressure from PSI to ATM
  pressureSensor2 = analogRead(Honeywell_2);                              //Reads the Honeywell Pressure Sensor's Analog Pin
  pressureSensorVoltage2 = pressureSensor2 * (5.0 / 1024);                //Converts the Digital Number to Voltage
  PressurePSI2 = (pressureSensorVoltage2 - (0.1 * 5.0)) / (4.0 / 15.0);   //Converts the Voltage to Pressure in PSI
  PressureATM2 = PressurePSI2 / 14.696;                                   //Converts Pressure from PSI to ATM
  PressureATMavg = (PressureATM1+PressureATM2)/2;                         //Creates an Averaged Pressure Between the Two Honeywell Pressure Sensors for Altitude Calculation

  //Altitude Prediction Calculation Based on Pressure
  AltitudeM = -6907.1662*log(PressureATMavg)-((0.01514904978)/sq(PressureATMavg))-(-30288.43208*pow(PressureATMavg, 6)+85760.41866*pow(PressureATMavg, 5)-81972.12542*pow(PressureATMavg, 4)+20481.51147*pow(PressureATMavg, 3)+14356.61783*sq(PressureATMavg)-8902.698208*(PressureATMavg)+546.362611);  //Converts Pressure in ATM to Altitude in Meters

  //Thermocouples #1 and #2 temperatures
  float T1int = maxthermo1.readThermocoupleTemperature();
  float T2int = maxthermo2.readThermocoupleTemperature();


  //Creates integers that can be changed, allowing the sig figs to be changed in the later strings depending on leading values, allowing for straight columns.
  int PR;                  //Honeywell pressure #1 sig fig integer
  int PS;                  //Honeywell pressure #2 sig fig integer
  int TT;                  //Thermocouple #1 temp sig fig integer
  int TU;                  //Thermocouple #2 temp sig fig integer

  if (0<PressureATM1) {        //Conditional function to detemine number of sig figs based on pressure (and temperature in lower statements) to keep columns organized.
    PR = 3;
  }
  else if (0>=PressureATM1) {
    PR = 2;
  }

  if (0<PressureATM2) {   
    PS = 3;
  }
  else if (0>=PressureATM2) {
    PS = 2;
  }

  if (T1int<=-10) {
    TT = 2;
  }
  else if (-10<T1int<=0 || 10<=T1int) {
    TT = 3;
  }
  else if (0<T1int<10) {
    TT = 4;
  }

  if (T2int<=-10) {
    TU = 2;
  }
  else if (-10<T2int<=0 || 10<= T2int) {
    TU = 3;
  }
  else if (0<T2int<10) {
    TU = 4;
  }
  
  String AtmSTR1 = String(PressureATM1, PR);                       //Converts Pressure to a string and uses sig figs based on conditional function above.
  String AtmSTR2 = String(PressureATM2, PS);      
  String TempSTR1 = String(T1int, TT);
  String TempSTR2 = String(T2int, TU);

  time = millis() / 1000 - 2;                                 //Converts time to seconds and starts the time at zero by subtracting the intial seconds.

  char TimeSTR[12];
  sprintf(TimeSTR, "%05u ", time);                            //Adds zeros in front of time


  String dataPRINT = AtmSTR1 + "           " + AtmSTR2 + "           " + TempSTR1 + "        " + TempSTR2 + "        " + TimeSTR + "      " + inputPressure1+inputPressure2+inputPressure3+inputPressure4+inputPressure5+inputPressure6 + "               " + AltitudeM;
  String dataLOG = AtmSTR1 + "," + AtmSTR2 + "," + TempSTR1 + "," + TempSTR2 + "," + TimeSTR + "," + inputPressure1+inputPressure2+inputPressure3+inputPressure4+inputPressure5+inputPressure6 + "," + AltitudeM;
  Serial.println(dataPRINT);
  xBee.println(dataPRINT);
    if (SDactive) {
      datalog = SD.open(filename, FILE_WRITE);
      datalog.println(dataLOG);                                //Takes serial monitor data and adds to SD card
      datalog.close();                                      //Close file afterward to ensure data is saved properly
    }

  
  inputPressure1 = " ";  //After printing/logging data, change input column back to blank
  inputPressure2 = " ";
  inputPressure3 = " ";
  inputPressure4 = " ";
  inputPressure5 = " ";
  inputPressure6 = " ";

  
  delay(466);  //CHANGE IF NEEDED TO ATTAIN ONE SECOND INTERVAL OF READINGS

}



void RECEIVED_COMMAND() {            //Incoming Command/Input Pressure

  if (xBee.available() >0){
    inputPressure1 = xBee.read();    //Save six received characters to later be printed in input column
    inputPressure2 = xBee.read();
    inputPressure3 = xBee.read();
    inputPressure4 = xBee.read();
    inputPressure5 = xBee.read();
    inputPressure6 = xBee.read();
    
    Serial.flush();                  //Clear receive buffer


    //If "R1.OFF" is typed into the monitor, relay #1 turns off 
    if (inputPressure1.startsWith('R') && inputPressure2.startsWith('1') && inputPressure3.startsWith('.') && inputPressure4.startsWith('O') && inputPressure5.startsWith('F') && inputPressure6.startsWith('F')) {
        switchStatus = false;
        resetStatus = true;
        SWITCH();
    }

    //If "R1..ON" is typed into the monitor, relay #1 turns on 
    if (inputPressure1.startsWith('R') && inputPressure2.startsWith('1') && inputPressure3.startsWith('.') && inputPressure4.startsWith('.') && inputPressure5.startsWith('O') && inputPressure6.startsWith('N')) {
        switchStatus = true;
        resetStatus = false;
        SWITCH();
    }

    //If "R2.OFF" is typed into the monitor, relay #2 turns off 
    if (inputPressure1.startsWith('R') && inputPressure2.startsWith('2') && inputPressure3.startsWith('.') && inputPressure4.startsWith('O') && inputPressure5.startsWith('F') && inputPressure6.startsWith('F')) {
        switchStatus2 = false;
        resetStatus2 = true;
        SWITCH2(); 
    }

    //If "R2..ON" is typed into the monitor, relay #2 turns on 
    if (inputPressure1.startsWith('R') && inputPressure2.startsWith('2') && inputPressure3.startsWith('.') && inputPressure4.startsWith('.') && inputPressure5.startsWith('O') && inputPressure6.startsWith('N')) {
        switchStatus2 = true;
        resetStatus2 = false;
        SWITCH2();
    }

  }
    
/*  
    switch (incomingByte) {
    
      case 'a':
      case 'A':                           //If received 'a' or 'A' turn relay #1 off
        switchStatus = false;
        resetStatus = true;
        SWITCH();
        break;

      case 'd':
      case 'D':                          //If received 'd' or 'D' turn relay #1 on   
        switchStatus = true;
        resetStatus = false;
        SWITCH();
        break;
        
      case 'z':
      case 'Z':                          //If received 'z' or 'Z' turn relay #2 off 
        switchStatus2 = false;
        resetStatus2 = true;
        SWITCH2(); 
        break;

      case 'c':
      case 'C':                          // If received 'c' or 'C' turn relay #2 on  
        switchStatus2 = true;
        resetStatus2 = false;
        SWITCH2();
        break;       

    }
  }
*/  
}



void SWITCH() {    //Mechanical switch stays in position it is told so keeping pins high is not necessary
  
  digitalWrite(switch_pin, switchStatus);
  digitalWrite(reset_pin, resetStatus);
  
  delay(100);
  
  digitalWrite(switch_pin, LOW);
  digitalWrite(reset_pin, LOW);
  
}

void SWITCH2() {    //Mechanical switch stays in position it is told so keeping pins high is not necessary
  
  digitalWrite(switch_pin_2, switchStatus2);
  digitalWrite(reset_pin_2, resetStatus2);
  
  delay(100);
  
  digitalWrite(switch_pin_2, LOW);
  digitalWrite(reset_pin_2, LOW);
  
}
