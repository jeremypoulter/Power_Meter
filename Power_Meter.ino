/*
 Reads voltage and power from China-style energy meter.
 Collecting data by eavesdropping on the MOSI-line (master in slave out)
 between the energy monitoring chip (ECH1560) and the main processor*/

#define CLKPin  2 // Pin connected to CLK (D2 & INT0)
#define MISOPin 5 // Pin connected to SDO (D5), from the CS5460A
#define MOSIPin 6 // Pin connected to SDI (D6), from the key pad/LCD MCU

// All variables that is changed in the interrupt function must be volatile to make sure changes are saved. 
volatile boolean clockTick = false;     // Set when a clock tick happens
volatile boolean byteComplete = false;  // Set when a byte is complete
volatile boolean gotSync = false;       // Set when we have detected the first long gap and we know we are at the start of a sequence of commands

volatile int sdiByte = 0;               // The SDI value
volatile int sdoByte = 0;               // The SDO value

// Set when we detect the long clock gap, cleared when first byte has been received
boolean startBlock = false;  

// Total number of bytes received
int byteCount = 0;

// The current command register value, see below
int commandRegister = 0;

// The value being read/written
uint32_t registerValue = 0;

// Number of bytes received in the packet
int packetByteCount = 0;

// The time of the last clock tick in ms
unsigned long lastClockTick = 0;

// Set to true if an error was detected when reading the packet
boolean packetError = false;

// The status of the CS5460
long status = 0;

// Commands
#define SYNC0                   B11111110
#define SYNC1                   B11111111

#define START_CONVERSION_SINGLE B11100000
#define START_CONVERSION_CONT   B11101000

#define POWER_UP                B10100000

#define POWER_DOWN_RESERVED_1   B10000000
#define POWER_DOWN_STAND_BY     B10010000
#define POWER_DOWN_SLEEP        B10001000
#define POWER_DOWN_RESERVED_2   B10011000

#define CALIBRATION_MASK        B11000000
#define CALIBRATION_MASK_VI     B00011000
#define CALIBRATION_MASK_R      B00000100
#define CALIBRATION_MASK_G      B00000010
#define CALIBRATION_MASK_O      B00000001

#define REGISTER_WRITE          B01000000
#define REGISTER_WRITE_MASK     B11000001

#define REGISTER_WRITE_Config   B01000000
#define REGISTER_WRITE_LDCoff   B01000010
#define REGISTER_WRITE_Ign      B01000100
#define REGISTER_WRITE_VDCoff   B01000110
#define REGISTER_WRITE_Vgn      B01001000
#define REGISTER_WRITE_Cycle    B01001010
#define REGISTER_WRITE_Pulse    B01001100
#define REGISTER_WRITE_I        B01001110
#define REGISTER_WRITE_V        B01010000
#define REGISTER_WRITE_P        B01010010
#define REGISTER_WRITE_E        B01010100
#define REGISTER_WRITE_I_RMS    B01010110
#define REGISTER_WRITE_V_RMS    B01011000
#define REGISTER_WRITE_TBC      B01011010
#define REGISTER_WRITE_Poff     B01011100
#define REGISTER_WRITE_Status   B01011110
#define REGISTER_WRITE_IACoff   B01100000
#define REGISTER_WRITE_VACoff   B01100010
#define REGISTER_WRITE_Test     B01110010
#define REGISTER_WRITE_Mask     B01110100
#define REGISTER_WRITE_Ctrl     B01110000

#define REGISTER_READ_Config    B00000000
#define REGISTER_READ_LDCoff    B00000010
#define REGISTER_READ_Ign       B00000100
#define REGISTER_READ_VDCoff    B00000110
#define REGISTER_READ_Vgn       B00001000
#define REGISTER_READ_Cycle     B00001010
#define REGISTER_READ_Pulse     B00001100
#define REGISTER_READ_I         B00001110
#define REGISTER_READ_V         B00010000
#define REGISTER_READ_P         B00010010
#define REGISTER_READ_E         B00010100
#define REGISTER_READ_I_RMS     B00010110
#define REGISTER_READ_V_RMS     B00011000
#define REGISTER_READ_TBC       B00011010
#define REGISTER_READ_Poff      B00011100
#define REGISTER_READ_Status    B00011110
#define REGISTER_READ_IACoff    B00100000
#define REGISTER_READ_VACoff    B00100010
#define REGISTER_READ_Test      B00110010
#define REGISTER_READ_Mask      B00110100
#define REGISTER_READ_Ctrl      B00110000

void setup()
{
  //Set the CLK-pin (D2) to input
  pinMode(CLKPin, INPUT);

  //Set the MISO-pin (D5) to input
  pinMode(MISOPin, INPUT);

  //Set the MOSI-pin (D6) to input
  pinMode(MOSIPin, INPUT);

  // Setting up interrupt ISR on D2 (INT0), trigger function "CLK_ISR()" when INT0 (CLK)is rising
  attachInterrupt(0, CLK_ISR, RISING);

  // initialize serial communications at 115200 bps: (to computer)
  pinMode(8, OUTPUT);    // initialize pin 8 to control the radio
  digitalWrite(8, HIGH); // select the radio
  Serial.begin(115200);
  Serial.println("Power Monitor!");

  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
}


double RegistValueToDouble(uint32_t registerValue, int pointPos)
{
  double fp = registerValue;
  return fp / (double)(1 << pointPos);
}

void loop()
{
  if (clockTick)
  {
    clockTick = false;
    lastClockTick = millis();
  }

  if (false == startBlock && millis() > lastClockTick + 4)
  {
    gotSync = true;
    startBlock = true;
    
    Serial.println("--");
    byteCount = 0;
    digitalWrite(13, HIGH);
  }

  if (byteComplete) 
  {
    byteComplete = false;

    byteCount++;

    if (startBlock) 
    {
      startBlock = false;

      digitalWrite(13, LOW);

      packetByteCount = 0;
      registerValue = 0;
      commandRegister = 0;
      packetError = false;
    }

    if (0 == packetByteCount)
    {
      commandRegister = sdiByte;
    }
    else if (SYNC0 == sdiByte && 0 == (commandRegister & REGISTER_WRITE_MASK))
    {
      registerValue = (registerValue << 8) | sdoByte;
    }
    else if (REGISTER_WRITE == (commandRegister & REGISTER_WRITE_MASK))
    {
      registerValue = (registerValue << 8) | sdiByte;
    }
    else
    {
//      Serial.print(sdiByte, HEX);
//      Serial.print("!");
//      packetError = true;
    }
      
    if (4 == ++packetByteCount)
    {
      if (false == packetError /* && SYNC1 != commandRegister && 0 != commandRegister */)
      {
        switch (commandRegister)
        {
          case SYNC1:
            break;
          case REGISTER_READ_Status:
            status = registerValue;
            break;
          case REGISTER_WRITE_Status:
            break;
          case REGISTER_READ_I_RMS:
            Serial.print("I: ");
            Serial.println(RegistValueToDouble(registerValue, 17));
            break;
          case REGISTER_READ_V_RMS:
            Serial.print("V: ");
            Serial.println(-RegistValueToDouble(registerValue, 15));
            break;
          case REGISTER_READ_E:
            break;
          default:
            Serial.print(">");
            Serial.print(commandRegister, BIN);
            Serial.print(REGISTER_WRITE == (commandRegister & REGISTER_WRITE_MASK) ? ">" : "<");
            Serial.println(registerValue);
            break;
        }
      }

      packetByteCount = 0;
      registerValue = 0;
      commandRegister = 0;
      packetError = false;
    }
  }
}

// Temp vars for the ISR to construct the bytes, gives loop a bit of time to process the data before being corrupted
int tempSdiByte = 0;
int tempSdoByte = 0;
int countBits = 0;      // Count read bits

// Function that triggers whenever CLK-pin is rising (goes high)
void CLK_ISR()
{
  int bits = PIND;

  clockTick = true;

  if(gotSync)
  {
    tempSdiByte = (tempSdiByte << 1) | ((bits >> MOSIPin) & 1);
    tempSdoByte = (tempSdoByte << 1) | ((bits >> MISOPin) & 1);

    if (8 == ++countBits) 
    {
      sdiByte = tempSdiByte;
      sdoByte = tempSdoByte;
      byteComplete = true;
      countBits = 0;
      tempSdiByte = 0;
      tempSdoByte = 0;
    }
  }
}

