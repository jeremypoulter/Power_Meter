/*
Reads voltage and power from China-style energy meter and sends it to nRF24L01+.
Collecting data from meter by eavesdropping on the MOSI-line (master in slave out)
between the energy monitoring chip (ECH1560) and the main processor
Meter            Arduino
GND (Brown)        GND          (2nd from the end of the meter-board)
VCC (3.6V)         RAW          (Connect to battery "+" under meter-board)
CLK (Green)        D2 (INT0)    (5th from the end of the meter-board)
SDO (Blue-white)   D5           (6th from the end of the meter-board)


nRF24L01+         Ardiono
GND (square)      GND
VCC               3,3V (mini = VCC)
CE                9
CSV               10
SCK               13
MOSI              11
MISO              12
*/

#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"

//NRF-setup:
#define CE_PIN   9
#define CSN_PIN 10

//Energy meter setup:
const int CLKPin = 2; // Pin connected to CLK (D2 & INT0)
const int MISOPin = 5;  // Pin connected to MISO (D5)


// Set up nRF24L01 radio on SPI bus plus pins 9 & 10
RF24 radio(CE_PIN, CSN_PIN); // Create a Radio


// Radio pipe addresses for 1 node to communicate.
//const uint64_t PipeAddress = 0xAABBCCDD11LL; //5-byte address "LL" is for long
const uint64_t pipes[] = { 0xAABBCCDD11LL };  //Transmitting address (Last 2 numbers are number on energy-meter)

//EnergyMeter-variables
//All variables that are changed in the interrupt function must be volatile to make sure changes are saved. 
volatile int Ba = 0;   //Store MISO-byte 1
volatile int Bb = 0;   //Store MISO-byte 2
volatile int Bc = 0;   //Store MISO-byte 2
float U = 0;    //voltage
float P = 0;    //power
float ReadData[3] = { 0, 0, 0 }; //Array to hold mean values of [U,I,P]

int AverageMax = 5;

volatile long CountBits = 0;      //Count read bits
volatile int Antal = 0;           //number of read values (to calculate mean)
volatile long ClkHighCount = 0;   //Number of CLK-highs (find start of a Byte)
volatile boolean inSync = false;  //as long as we ar in SPI-sync
volatile boolean NextBit = true;  //A new bit is detected

void SendToComputer(void); //initiate transmitt-function

void setup(void)
{
  //debug over Serial
  Serial.begin(57600);

  //Setting up interrupt ISR on D2 (INT0), trigger function "CLK_ISR()" when INT0 (CLK)is rising
  attachInterrupt(0, CLK_ISR, RISING);

  //Set the CLK-pin (D5) to input
  pinMode(CLKPin, INPUT);

  //Set the MISO-pin (D5) to input
  pinMode(MISOPin, INPUT);



  // Setup and configure nrf radio see: http://maniacbug.github.io/RF24/classRF24.html
  radio.begin();

  //
  //********* Optional settings for nrf below see: http://maniacbug.github.io/RF24/classRF24.html
  //

  // increase the delay between retries & # of retries
  radio.setRetries(15, 15);

  // reduce the payload size. (3 floates = 3*4=12 Bytes)
  radio.setPayloadSize(12);

  //Set speed of transmission (250kbps only works on the "+"-version!!! slower increases range!
  radio.setDataRate(RF24_250KBPS);

  //Set Power Amplifier (PA) level to high to increase range!
  radio.setPALevel(RF24_PA_HIGH);

  //Automatically receive data when sending something
  radio.enableAckPayload();

  //
  //********* End of optional settings********* 
  //

  //Addresses has to be set last!
  //Transmitter address
  radio.openWritingPipe(pipes[0]);
  //I don't have to set an receiver address, because the auto ack will automatically receive an payload if i need to send anything to this meter!
}

void loop(void)
{
  //do nothing until the CLK-interrupt occures and sets inSync=true
  if (inSync == true){
    CountBits = 0;  //CLK-interrupt increments CountBits when new bit is received
    while (CountBits<40){}  //skip the uninteresting 5 first bytes
    CountBits = 0;
    Ba = 0;
    Bb = 0;
    while (CountBits<24){  //Loop through the next 3 Bytes (6-8) and save byte 6 and 7 in Ba and Bb
      if (NextBit == true){ //when rising edge on CLK is detected, NextBit = true in in interrupt. 
        if (CountBits < 9){  //first Byte/8 bits in Ba
          Ba = (Ba << 1);  //Shift Ba one bit to left and store MISO-value (0 or 1) (see http://arduino.cc/en/Reference/Bitshift)
          //read MISO-pin, if high: make Ba[0] = 1
          if (digitalRead(MISOPin) == HIGH){
            Ba |= (1 << 0);  //changes first bit of Ba to "1"
          }   //doesn't need "else" because BaBb[0] is zero if not changed.
          NextBit = false; //reset NextBit in wait for next CLK-interrupt
        }
        else if (CountBits < 17){  //bit 9-16 is byte 7, stor in Bb
          Bb = Bb << 1;  //Shift Ba one bit to left and store MISO-value (0 or 1)
          //read MISO-pin, if high: make Ba[0] = 1
          if (digitalRead(MISOPin) == HIGH){
            Bb |= (1 << 0);  //changes first bit of Bb to "1"
          }
          NextBit = false;  //reset NextBit in wait for next CLK-interrupt
        }
      }
    }
    if (Bb != 3){ //if bit Bb is not 3, we have reached the important part, U is allready in Ba and Bb and next 8 Bytes will give us the Power. 
      Antal += 1;  //increment for mean value calculations

      //Voltage = 2*Ba+Bb/255
      U = 2.0*((float)Ba + (float)Bb / 255.0);

      //Power:
      CountBits = 0;
      while (CountBits<40){}//Start reading the next 8 Bytes by skipping the first 5 uninteresting ones

      CountBits = 0;
      Ba = 0;
      Bb = 0;
      Bc = 0;
      while (CountBits<24){  //store byte 6, 7 and 8 in Ba and Bb & Bc. 
        if (NextBit == true){
          if (CountBits < 9){
            Ba = (Ba << 1);  //Shift Ba one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Ba[0] = 1
            if (digitalRead(MISOPin) == HIGH){
              Ba |= (1 << 0);  //changes first bit of Ba to "1"
            }
            NextBit = false;
          }
          else if (CountBits < 17){
            Bb = Bb << 1;  //Shift Ba one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Ba[0] = 1
            if (digitalRead(MISOPin) == HIGH){
              Bb |= (1 << 0);  //changes first bit of Bb to "1"
            }
            NextBit = false;
          }
          else{
            Bc = Bc << 1;  //Shift Bc one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Bc[0] = 1
            if (digitalRead(MISOPin) == HIGH){
              Bc |= (1 << 0);  //changes first bit of Bc to "1"
            }
            NextBit = false;
          }
        }

      }

      //Power = (Ba*255+Bb)/2
      P = ((float)Ba * 255 + (float)Bb + (float)Bc / 255.0) / 2;

      //Voltage mean
      ReadData[0] = (U + ReadData[0] * ((float)Antal - 1)) / (float)Antal;
      //Power mean
      ReadData[1] = (P + ReadData[2] * ((float)Antal - 1)) / (float)Antal;
      //Power max
      if (P>ReadData[2]){
        ReadData[2] = P;
      }

      //every 5th read-out ~every 5th second, we send the mean value data to the nrf24l01-transmitter
      if (Antal >= AverageMax){
        //Send the data to the computer
        bool ok = radio.write(ReadData, sizeof(ReadData));
        if (ok){
          Antal = 0;  //reset mean-value counter after successfull transmission ~10s
          ReadData[0] = 0;
          ReadData[1] = 0;
          ReadData[2] = 0;

          //This is an awesome feature, if "radio.enableAckPayload()" is enabled, then we may have got somethig in return that we can use:
          //did we get any data in return when sending the bytes??
          if (radio.isAckPayloadAvailable())
          {
            radio.read(&AverageMax, sizeof(AverageMax));
          }
        }
      }
      inSync = false;  //reset sync variable to make sure next reading is in sync. 

    }


    if (Bb == 0){  //If Bb is not 3 or something else than 0, something is wrong! 
      inSync = false;
      Serial.println("Disconnected");
    }


  }
}

//Function that triggers whenever CLK-pin is rising (goes high)
void CLK_ISR(){
  //if we are trying to find the sync-time (CLK goes high for 1-2ms)
  if (inSync == false){
    ClkHighCount = 0;
    //Register how long the ClkHigh is high to evaluate if we are at the part wher clk goes high for 1-2 ms
    while (digitalRead(CLKPin) == HIGH){
      ClkHighCount += 1;
      delayMicroseconds(30);  //can only use delayMicroseconds in an interrupt. 
    }
    //if the Clk was high between 1 and 2 ms than, its a start of a SPI-transmission
    if (ClkHighCount >= 33 && ClkHighCount <= 67){
      inSync = true;
    }
  }
  else{ //we are in sync and logging CLK-highs
    //increment an integer to keep track of how many bits we have read. 
    CountBits += 1;
    NextBit = true;
  }
}

// vim:cin:ai:sts=2 sw=2 ft=cpp