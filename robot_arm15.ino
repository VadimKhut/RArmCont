#include <Servo.h>
#include <IRremote.h>
#include <SdFat.h>            //  https://github.com/greiman/SdFat
#include <TM1637Display.h>    //  https://github.com/avishorp/TM1637

/*
   The Arduino connections are as follows:

    D2 - joystick select button
    D3 - IR diode
    D4 - rotating base servo
    D5 - claw servo
    D6 - vertical arm motion servo
    D7 - horizontal arm motion servo
    D8 - LCD CLK
    D9 - LCD DIO
    D10 - SD card CS
    D11 - SD card MOSI
    D12 - SD card MISO
    D13 - SD card CK
    A0 - record button
    A1 - playback button
    A2 - unused
    A3 - potentiometer
    A4 - close claw button
    A5 - open claw button
    A6 - joystick vertical
    A7 - joystick horizontal

*/



// Note that A6 and A7 are analog input only on Arduino Mini.

// LED module connection pins (Digital Pins)
#define CLK 8
#define DIO 9

TM1637Display display(CLK, DIO);
int k;
uint8_t data[] = { 0xff, 0xff, 0xff, 0xff };
 
Servo base;
Servo claw;
Servo vArm;
Servo hArm;

// SD shield
const int chipSelect = 10;
SdFat sd;                    // CS/MOSI/MISO/CK = 10/11/12/13
SdFile myFile;
char name[] = "ARM000.CSV"; // Will be incremented to create a new file each run.

// For control of the arm servos
const int VERT = A6; // analog
const int HORIZ = A7; // analog
const int SEL = 2; // digital

// For turning the base
const int potPin = A3;
int potValue = 0;

// To open/close the claw
const int blackButton = A5;  // open claw
const int whiteButton = A4;  // close claw

int basePos;
int old_basePos = 0;
int temp_basePos;
boolean baseIRcontrol = false;
int clawPosition = 80;  // 60 is closed
int vPos = 90;
int hPos = 90;

// R = Recording
// S = Select playback program
// P = Playback
// N = None
char mode = 'N'; 
int recordButton   = A0;
int playbackButton = A1;
long int recStart;
int playbackProgram = -1;
int maxProgram = 0;

// IR stuff:
#define NUM_BUTTONS 9 // The remote has 9 buttons
const uint16_t BUTTON_POWER = 0xD827; // i.e. 0x10EFD827
const uint16_t BUTTON_A = 0xF807;
const uint16_t BUTTON_B = 0x7887;
const uint16_t BUTTON_C = 0x58A7;
const uint16_t BUTTON_UP = 0xA05F;
const uint16_t BUTTON_DOWN = 0x00FF;
const uint16_t BUTTON_LEFT = 0x10EF;
const uint16_t BUTTON_RIGHT = 0x807F;
const uint16_t BUTTON_CIRCLE = 0x20DF;
/* Connect the output of the IR receiver diode to pin 11. */
int RECV_PIN = 3;
/* Initialize the irrecv part of the IRremote  library */
IRrecv irrecv(RECV_PIN);
decode_results results; // This will store our IR received codes
uint16_t lastCode = 0; // This keeps track of the last code RX'd


void setup()
{
  // make the SEL line an input
  pinMode(SEL,INPUT);
  // turn on the pull-up resistor for the SEL line (see http://arduino.cc/en/Tutorial/DigitalPins)
  digitalWrite(SEL,HIGH);

  pinMode(blackButton,INPUT_PULLUP);
  pinMode(whiteButton,INPUT_PULLUP);

  base.attach(4);
  claw.attach(5);
  vArm.attach(6);
  hArm.attach(7);

  // set up serial port for output
  Serial.begin(9600);
  Serial.println("Starting...");

  pinMode(recordButton,INPUT_PULLUP);
  pinMode(playbackButton,INPUT_PULLUP);

  // Initialize the display
  display.setBrightness(7); // Turn on
  for(int k = 0; k < 4; k++) data[k] = 0;
  display.setSegments(data);

  irrecv.enableIRIn(); // Start the receiver
}

void writeCommand(char inCommand, int inValue)
{
  if (mode == 'R') {
    Serial.print("writeCommand: ");
    Serial.print(inCommand);
    Serial.println(inValue);
    if (myFile.open(name, O_RDWR | O_CREAT | O_AT_END)) {
      myFile.print(millis() - recStart);    
      myFile.print(",");
      myFile.print(inCommand);
      myFile.print(",");
      myFile.println(inValue);
      myFile.close();
    }
  }
}

void startRecord()
{
  // Look at the SD card and figure out the new file name to use.
  // Then initialize the file.
  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {
    Serial.println ("Cannot access local SD card.");
  }
  else {
    // Set the output file name
    for (uint8_t i = 0; i < 1000; i++) {
      setName(i);
      if (sd.exists(name)) continue;
      playbackProgram = i;
      break;
    }
    Serial.print("SD card output file is ");
    Serial.println(name);
  }
  if (!myFile.open(name, O_RDWR | O_CREAT | O_AT_END)) {
    Serial.println ("Opening of local SD card FAILED.");
  }
  else {
    Serial.println("Lighting up display");
    //myFile.println("time,command,value");    
    // Need to first write the initial (current) position
    // of the arm so it can be initialized upon playback.
    myFile.print("0,V,"); myFile.println(vPos);
    myFile.print("0,H,"); myFile.println(hPos);
    myFile.print("0,B,"); myFile.println(basePos);
    myFile.print("0,C,"); myFile.println(clawPosition);
    myFile.close();
    mode = 'R';
    recStart = millis();
    setDisplay('R');
  }
}

void startSelect() 
{
  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {
    Serial.println ("Cannot access local SD card.");
  }
  mode = 'S';
  // Find maximum program number
  for (uint8_t i = 0; i < 1000; i++) {
    setName(i);
    if (sd.exists(name)) continue;
    maxProgram = i-1;
    break;
  }
  // Get program to playback
  if (playbackProgram == -1 ) {
    // Find latest recorded program and use that.
    for (uint8_t i = 0; i < 1000; i++) {
      setName(i);
      if (sd.exists(name)) continue;
      playbackProgram = i-1;
      break;
    }
  }
  setName(playbackProgram);
  setDisplay('S');
  delay(500);
}

void setName(int i){
  name[3] = i/100 + '0';
  name[4] = (i%100)/10 + '0';
  name[5] = i%10 + '0';
}

void setDisplay(char inMode) {
  display.setBrightness(7); // Turn on
  switch (inMode) {
    case 'R':
      data[0] = 0b01010000;
      break;
    case 'P':
      data[0] = 0b01110011;
      break;
    case 'S':
      //data[0] = 0b01101101;
      // Changed this from S to C since S looks just like % on LED.
      data[0] = 0b00111001;
      break;
  }
  data[1] = display.encodeDigit((int)name[3]);
  data[2] = display.encodeDigit((int)name[4]);
  data[3] = display.encodeDigit((int)name[5]);
  display.setSegments(data);
}


void startPlayback(int in_playbackProgram) {
  setName(in_playbackProgram);
  setDisplay('P');
  mode = 'P';
  delay(1000);    
  Serial.print("Playing back file ");
  Serial.println(name);

  long int commandTime;
  char commandType;
  int commandParam;
  char c1, c2;   // commas
  long int fileLine = 0;
  long int commandTimePrev = 0;

  ifstream sdin(name);
  if (sdin.is_open()) {
    while (sdin >> commandTime >> c1 >> commandType >> c2 >> commandParam) {
      if (c1 != ',' || c2 != ',') continue;
      fileLine++;
      switch (commandType) {
        case 'C':
          claw.write(commandParam);
          break;
        case 'B':
          base.write(commandParam);
          break;
        case 'H':
          hArm.write(commandParam);
          break;
        case 'V':
          vArm.write(commandParam);
          break;
      }
      if (commandTime == 0) {
        delay(500);
      }
      else {
        delay(commandTime - commandTimePrev);
        commandTimePrev = commandTime;
      }
    }
  }
  mode = 'N';
  for(int k = 0; k < 4; k++) data[k] = 0;
  display.setSegments(data);
}


void loop() 
{
  int vertical, horizontal, select;

  // First check to see if we need to record or stop.
  if (!digitalRead(recordButton))
  {
    Serial.println("record button press!");
    if (mode == 'R') 
    { 
      mode = 'N'; 
      // Turn off display
      Serial.println("Turning off display.");
      for(int k = 0; k < 4; k++) data[k] = 0;
      display.setSegments(data);
      delay(2000);
    }
    else
    {
      startRecord();
      delay(2000);
    }
  }

  // Now check if we need to playback...
  if (!digitalRead(playbackButton))
  {
    if (mode == 'S') {
      // Selection has been made. Start playback.
      startPlayback(playbackProgram);
    }
    else {
      Serial.println("play button press!");
      startSelect();
    }
  }
  
  vertical = analogRead(VERT); // will be 0-1023
  vertical = 1023-vertical;
  horizontal = analogRead(HORIZ); // will be 0-1023
  select = digitalRead(SEL); // will be HIGH (1) if not pressed, and LOW (0) if pressed
  potValue = analogRead(potPin);

  // If IR commands for base turning have been issues, disable code to move base to 
  // position indicated by potentiometer until further potentiometer movement it detected.
  temp_basePos = map(potValue,1,1023,0,180);
  if (abs(temp_basePos - old_basePos) > 5) {
    baseIRcontrol = false;
  }
  if (!baseIRcontrol) {
    basePos = temp_basePos;
    if (basePos != old_basePos) {
      base.write(basePos);
      writeCommand('B', basePos);
      old_basePos = basePos;
    }
  }

  // Putting in this delay so the robot does not move too fast.
  // If recording, that includes some delay already.
  if (mode == 'R') delay(15);
  else delay(30);

  if (digitalRead(blackButton) && clawPosition <=175) {clawPosition += 5; writeCommand('C', clawPosition); }
  if (digitalRead(whiteButton) && clawPosition>=65)   {clawPosition -= 5; writeCommand('C', clawPosition); }
  claw.write(clawPosition);

  // Full, sudden release of claw when joystick pressed.
  if (select == LOW) {
    clawPosition = 150;
    writeCommand('C', clawPosition);
    claw.write(clawPosition);
  }

  // If we are in "select" mode, use the joystick to select a program
  if (mode == 'S') {
    if (playbackProgram < maxProgram && horizontal>700) {
      playbackProgram++;
      setName(playbackProgram);
      setDisplay('S');
      delay(500);
    }
    if (playbackProgram > 1 && horizontal<300) {
      playbackProgram--;
      setName(playbackProgram);
      setDisplay('S');
      delay(500);
    }
  }
  else {
    if (vertical<300 && vPos<=130) {vPos += 2; writeCommand('V', vPos); }
    if (vertical>700 && vPos>=18)  {vPos -= 2; writeCommand('V', vPos); }
    vArm.write(vPos);

    if (horizontal>700 && hPos<=130) {hPos += 2; writeCommand('H', hPos); }
    if (horizontal<300 && hPos>=26)  {hPos -= 2; writeCommand('H', hPos); }
    hArm.write(hPos);
  }
  
  // IR code
    if (irrecv.decode(&results)) 
  {
    /* read the RX'd IR into a 16-bit variable: */
    uint16_t resultCode = (results.value & 0xFFFF);

    /* The remote will continue to spit out 0xFFFFFFFF if a 
     button is held down. If we get 0xFFFFFFF, let's just
     assume the previously pressed button is being held down */
    if (resultCode == 0xFFFF)
      resultCode = lastCode;
    else
      lastCode = resultCode;

    // This switch statement checks the received IR code against
    // all of the known codes. Each button press produces a 
    // serial output, and has an effect on the LED output.
    switch (resultCode)
    {
      case BUTTON_POWER:
        Serial.println("Power");
        if (clawPosition>=65) clawPosition -= 5;
        claw.write(clawPosition);
        break;
      case BUTTON_CIRCLE:
        Serial.println("Circle");
        if (clawPosition <=175) clawPosition += 5;
        claw.write(clawPosition);
        break;
      case BUTTON_B:
        Serial.println("B");
        clawPosition = 150;
        claw.write(clawPosition);
        break;
      case BUTTON_A:
        Serial.println("A");
        baseIRcontrol = true;
        if (basePos <=175) basePos += 2;
        base.write(basePos);
        break;
      case BUTTON_C:
        baseIRcontrol = true;
        Serial.println("C");
        if (basePos>=65) basePos -= 2;
        base.write(basePos);
        break;
      case BUTTON_RIGHT:
        if (vPos<=130) vPos += 2;
        vArm.write(vPos);
        break;
      case BUTTON_LEFT:
        if (vPos>=18) vPos -= 2;
        vArm.write(vPos);
        break;
      case BUTTON_DOWN:
        if (hPos>=26) hPos -= 2;
        hArm.write(hPos);
        break;
      case BUTTON_UP:
        if (hPos<=130) hPos += 2;
        hArm.write(hPos);
        break;
      default:
        Serial.print("Unrecognized code received: 0x");
        Serial.println(results.value, HEX);
        break;        
    }    
    irrecv.resume(); // Receive the next value
  }

}  
