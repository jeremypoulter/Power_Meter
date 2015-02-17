/*
 Reads voltage and power from China-style energy meter. 
 Collecting data by eavesdropping on the MOSI-line (master in slave out) 
 between the energy monitoring chip (ECH1560) and the main processor*/
 
const int CLKPin = 2; // Pin connected to CLK (D2 & INT0)
const int MISOPin = 5;  // Pin connected to MISO (D5)
 
//All variables that is changed in the interrupt function must be volatile to make sure changes are saved. 
volatile int Ba = 0;   //Store MISO-byte 1
volatile int Bb = 0;   //Store MISO-byte 2
volatile int Bc = 0;   //Store MISO-byte 2
float U = 0;    //voltage
float P = 0;    //power
float ReadData[3] = {0, 0, 0}; //Array to hold mean values of [U,I,P]
 
volatile long CountBits = 0;      //Count read bits
volatile int Antal = 0;           //number of read values (to calculate mean)
volatile long ClkHighCount = 0;   //Number of CLK-highs (find start of a Byte)
volatile boolean inSync = false;  //as long as we ar in SPI-sync
volatile boolean NextBit = true;  //A new bit is detected
 
void setup() {
  //Setting up interrupt ISR on D2 (INT0), trigger function "CLK_ISR()" when INT0 (CLK)is rising
  attachInterrupt(0, CLK_ISR, RISING);
 
  //Set the CLK-pin (D5) to input
  pinMode(CLKPin, INPUT);
  
  //Set the MISO-pin (D5) to input
  pinMode(MISOPin, INPUT);
  
  // initialize serial communications at 9600 bps: (to computer)
  pinMode(8, OUTPUT);    // initialize pin 8 to control the radio
  digitalWrite(8, HIGH); // select the radio
  Serial.begin(115200); 

  pinMode(13, OUTPUT);
}
 
void loop() {
  digitalWrite(13, inSync ? LOW : HIGH);

  //do nothing until the CLK-interrupt occures and sets inSync=true
  if(inSync == true){
    CountBits = 0;  //CLK-interrupt increments CountBits when new bit is received
    while(CountBits<40){}  //skip the uninteresting 5 first bytes
    CountBits=0;
    Ba=0;
    Bb=0;
    while(CountBits<24){  //Loop through the next 3 Bytes (6-8) and save byte 6 and 7 in Ba and Bb
      if(NextBit == true){ //when rising edge on CLK is detected, NextBit = true in in interrupt. 
        if(CountBits < 9){  //first Byte/8 bits in Ba
          Ba = (Ba << 1);  //Shift Ba one bit to left and store MISO-value (0 or 1) (see http://arduino.cc/en/Reference/Bitshift)
          //read MISO-pin, if high: make Ba[0] = 1
          if(digitalRead(MISOPin)==HIGH){
            Ba |= (1<<0);  //changes first bit of Ba to "1"
          }   //doesn't need "else" because BaBb[0] is zero if not changed.
          NextBit=false; //reset NextBit in wait for next CLK-interrupt
        }
        else if(CountBits < 17){  //bit 9-16 is byte 7, stor in Bb
          Bb = Bb << 1;  //Shift Ba one bit to left and store MISO-value (0 or 1)
          //read MISO-pin, if high: make Ba[0] = 1
          if(digitalRead(MISOPin)==HIGH){
            Bb |= (1<<0);  //changes first bit of Bb to "1"
          }
          NextBit=false;  //reset NextBit in wait for next CLK-interrupt
        }
      }
    }
    if(Bb!=3){ //if bit Bb is not 3, we have reached the important part, U is allready in Ba and Bb and next 8 Bytes will give us the Power. 
      Antal += 1;  //increment for mean value calculations
      
     //Voltage = 2*(Ba+Bb/255)
      U=2.0*((float)Ba+(float)Bb/255.0); 
      
      //Power:
      CountBits=0;
      while(CountBits<40){}//Start reading the next 8 Bytes by skipping the first 5 uninteresting ones
      
      CountBits=0;
      Ba=0;
      Bb=0;
      Bc=0;
      while(CountBits<24){  //store byte 6, 7 and 8 in Ba and Bb & Bc. 
        if(NextBit == true){
          if(CountBits < 9){
            Ba = (Ba << 1);  //Shift Ba one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Ba[0] = 1
            if(digitalRead(MISOPin)==HIGH){
              Ba |= (1<<0);  //changes first bit of Ba to "1"
            } 
            NextBit=false;
          }
          else if(CountBits < 17){
            Bb = Bb << 1;  //Shift Ba one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Ba[0] = 1
            if(digitalRead(MISOPin)==HIGH){
              Bb |= (1<<0);  //changes first bit of Bb to "1"
            }
            NextBit=false;
          }
          else{
            Bc = Bc << 1;  //Shift Bc one bit to left and store MISO-value (0 or 1)
            //read MISO-pin, if high: make Bc[0] = 1
            if(digitalRead(MISOPin)==HIGH){
              Bc |= (1<<0);  //changes first bit of Bc to "1"
            }
            NextBit=false;
          }
        }
        
      }
      
      Ba=255-Ba;
      Bb=255-Bb;
      Bc=255-Bc; 

      //Power = (Ba*255+Bb)/2
      P=((float)Ba*255+(float)Bb+(float)Bc/255.0)/2;
      
      //Voltage mean
      ReadData[0] = (U+ReadData[0]*((float)Antal-1))/(float)Antal;
      //Current mean
      ReadData[1] = (P/U+ReadData[1]*((float)Antal-1))/(float)Antal;
      //Power mean
      ReadData[2] = (P+ReadData[2]*((float)Antal-1))/(float)Antal;
      
      //Print out results (i skipped the mean values since the actual ones are very stable)
      Serial.print("U: ");
      Serial.print(U,1);
      Serial.println("V");
      Serial.print("I: ");
      Serial.print(P/U*1000,0);  //I=P/U and in milli ampere
      Serial.println("mA");
      Serial.print("P: ");
      Serial.print(P,1);
      Serial.println("W");
      Serial.println("");
      
      if(Antal==10){ //every 10th 70-package = every ~10s
        //transmit ReadData-array to nRF or Wifi-module here:
        //transmission function here...
        
        //Reset ReadData-array
        ReadData[0] = 0;
        ReadData[1] = 0;
        ReadData[2] = 0;
        //reset mean-value counter 
        Antal=0;  
      }
      inSync=false;  //reset sync variable to make sure next reading is in sync. 
      
    }
    
    
    if(Bb==0){  //If Bb is not 3 or something else than 0, something is wrong! 
      inSync=false;
      Serial.println("Nothing connected, or out of sync!");
    }
  }
}
 
//Function that triggers whenever CLK-pin is rising (goes high)
void CLK_ISR(){
  //if we are trying to find the sync-time (CLK goes high for 1-2ms)
  if(inSync==false){
    ClkHighCount = 0;
    //Register how long the ClkHigh is high to evaluate if we are at the part wher clk goes high for 1-2 ms
    while(digitalRead(CLKPin)==HIGH){
      ClkHighCount += 1;
      delayMicroseconds(30);  //can only use delayMicroseconds in an interrupt. 
    }
    //if the Clk was high between 1 and 2 ms than, its a start of a SPI-transmission
    if(ClkHighCount >= 33 && ClkHighCount <= 67){
       inSync = true;
    }
  }
  else{ //we are in sync and logging CLK-highs
    //increment an integer to keep track of how many bits we have read. 
    CountBits += 1; 
    NextBit = true;
  }
}

