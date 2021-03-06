
#include <RFM12B.h>
#include <SPI.h>
#include "bma2XX_regs.h"
#include <CounterLib_t.h>

#include "Energia.h"

Counter<> freqCounter;     // create counter that counts pulses on pin P1.0

#define NODEID        42   //network ID used for this unit
#define NETWORKID     137  //the network ID we are on
#define GATEWAYID     1    //the node ID we're sending to

#define LED       //disbable modules by uncommenting
#define RADIO
#define BUZZER
#define ACC
//#define DEBUG

#define WINFREQ   500
#define STARTFREQ 100

/*
 * Gamemode 1: green. Only reacts on wire contact.
 * Gamemode 2: blue.  Triggers on wire contact, and too slow motion.
 * Gamemode 3: red.   Triggers on wire contact, too slow motion and change of oriantation.
 */
  
/* VBAT = ADC * 0,00244V
 * 614  = 1,5V
 * 574  = 1,4V
 * 533  = 1,3V
 * 492  = 1,2V
 */


#define FILTER   //prefilter for some Frequencies (100, 200, 300, 400, 500, 600kHz)

#define FREQDELAY 2   //tune this for better frequency readout
#define AQUISITION 50 //how lonh to wait for frequency measurement
#define MINFREQ 90    //lowest used frequency


#define START               digitalWrite(CS_BMA, 0)
#define STOP                digitalWrite(CS_BMA, 1)
#define READ                0x80

#define LED_G P2_4 
#define LED_R P2_2
#define LED_B P2_1

#define BUZZ P2_5

#define BUTTON P2_3

#define CS_RFM12 P3_0
#define CS_BMA   P3_1

#define ADC P1_4
#define WIRE P1_0
#define INT_BMA  P1_3


#define ACK_TIME     1500  // # of ms to wait for an ack
#define SERIAL_BAUD  9600  // serial debug baud rate
#define requestACK 0       //request ack 

#define RED 2
#define GREEN 0
#define BLUE 1

RFM12B radio;
byte sendSize = 0;
char payload[28] = ""; // max. 127 bytes
uint16_t history[3] = {0, 0, 0};


byte data[3];
boolean buttonInterrupt = false, wireInterrupt = false, accInterrupt = false;

uint16_t batVoltage = 0;
byte gameMode = 0;



  
void setup() {                
  // initialize the digital pin as an output.
  pinMode(LED_R, OUTPUT);    
  pinMode(LED_G, OUTPUT); 
  pinMode(LED_B, OUTPUT);  
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
  pinMode(BUZZ, OUTPUT);  
  digitalWrite(BUZZ, LOW);

  pinMode(ADC, INPUT); 
  analogReference(INTERNAL2V5);

  pinMode(CS_RFM12, OUTPUT);  
  digitalWrite(CS_RFM12, HIGH);

  pinMode(CS_BMA, OUTPUT);
  digitalWrite(CS_BMA, HIGH);

  #ifdef DEBUG
  Serial.begin(SERIAL_BAUD);
  Serial.print("start...");
  #endif

  SPI.begin();
  SPI.setClockDivider(2); //8MHz SPI clock

  #ifdef RADIO
  radio.Initialize(NODEID, RF12_433MHZ, NETWORKID);
  radio.Sleep(); //sleep right away to save power
  #endif

  pinMode(BUTTON, INPUT_PULLUP);
  attachInterrupt(BUTTON, buttonFunction, FALLING);  //interrupt for button

  #ifdef ACC
  bma2XXclearInterrupts(); //clear existing interrupts
  delay(100);
  bma2XXsetProfile(); //initialize Accelerometer
  //attachInterrupt(INT_BMA, accFunction, FALLING); //interrupt for BMA280
  #endif

  //pinMode(WIRE, INPUT);
  attachInterrupt(WIRE, wireFunction, CHANGE);  //interrupt for button
  

  //enableComparator();
  //_BIS_SR(LPM4_bits + GIE); //sleep, wait for interrupts
  //interrupts();
  suspend();
}

void loop() {
  if (buttonInterrupt) {
    buttonInterrupt = false;
    for (byte i = 0; i < 255; i++) { //wait ~5s
      if (digitalRead(BUTTON) != LOW) {
        delay(1000);
        if (digitalRead(BUTTON) == LOW) {
          gameMode++;
          if (gameMode > 2)
            gameMode = 0;

          if (gameMode == 0)
            detachInterrupt(INT_BMA);

          else
            attachInterrupt(INT_BMA, accFunction, FALLING);
            //noInterrupts();*/
          fail(gameMode);
        }
        return;
      }
      delay(20);
    }

    for (byte i = 0; i < 2; i++) { //indicator flashing
      digitalWrite(LED_B, HIGH);
      delay(100);
      digitalWrite(LED_B, LOW);
      delay(100);
    }
    
    delay(1000);
    
    if (digitalRead(BUTTON) != LOW) { //button still pressed?
      deepSleep(); //...then go to deep sleep
      
      #ifdef ACC   //This is the point we wake up again later
      bma2XXclearInterrupts(); 
      delay(100);
      bma2XXsetProfile();
      attachInterrupt(INT_BMA, accFunction, FALLING);
      #endif
      digitalWrite(LED_G, HIGH);
      delay(200);
      digitalWrite(LED_G, LOW);
      #ifdef BUZZER
      bootMelody(); //Windows XP boot melody ;)
      #endif
      attachInterrupt(WIRE, wireFunction, FALLING);
      return;
    }

    delay(1000); //not pressed?
    selfTest();  //then do a self test
  }

  if (wireInterrupt) {
    wireInterrupt = false;
    //CACTL1 = 0; //disable comparator
    //CACTL2 = 0;
    //reuse interrupt Pin from frequency measurement
    long currentTime = millis();
    boolean freqValid = false, freqStart = false, freqWin = false;;
    freqCounter.start(CL_Div8); //start Timer  with 8x divider
    while (millis() - currentTime < AQUISITION) {
      freqCounter.reset();
      delay(FREQDELAY);  
      history[0] = ((freqCounter.read()) / FREQDELAY * 8) / 1.28;
      if ((history[1] < history[2] + 1 || history[1] > history[2] - 1) && (history[0] < history[1] + 1 || history[0] > history[1] - 1) && history[0] > 90) { //compare three measurements
        #ifdef FILTER
        if (history[0] < 110 && history[0] > 90) {
          freqValid = true;
          #if STARTFREQ == 100
          freqStart = true;
          #endif
          #if WINFREQ == 100
          freqWin = true;
          #endif
          break;
        }

        if (history[0] < 210 && history[0] > 190) {
          freqValid = true;
          #if STARTFREQ == 200
          freqStart = true;
          #endif
          #if WINFREQ == 200
          freqWin = true;
          #endif
          break;
        }
        
        if (history[0] < 310 && history[0] > 290) {
          freqValid = true;
          #if STARTFREQ == 300
          freqStart = true;
          #endif
          #if WINFREQ == 300
          freqWin = true;
          #endif
          break;
        }
        
        if (history[0] < 410 && history[0] > 390) {
          freqValid = true;
          #if STARTFREQ == 400
          freqStart = true;
          #endif
          #if WINFREQ == 400
          freqWin = true;
          #endif
          break;
        }
        
        if (history[0] < 510 && history[0] > 490) {
          freqValid = true;
          #if STARTFREQ == 500
          freqStart = true;
          #endif
          #if WINFREQ == 500
          freqWin = true;
          #endif
          break;
        }
        
        if (history[0] < 610 && history[0] > 590) {
          freqValid = true;
          #if STARTFREQ == 600
          freqStart = true;
          #endif
          #if WINFREQ == 600
          freqWin = true;
          #endif
          break;
        }
        #endif
        
        #ifndef FILTER
        freqValid = true;
        break;
        #endif
      }
      history[2] = history[1];
      history[1] = history[0];
      delay(FREQDELAY);
    }
    freqCounter.stop(); //stop Timer
    if (!freqValid) {
      history[0] = 999;
    }
    sendPackage(2, history[0]);
    initTimers(); //restore system Timers
    if (freqStart) {
      startMelody();
    }
    else if (freqWin) {
      for (byte i = 0; i < 2; i++) { //indicator flashing
      digitalWrite(LED_B, HIGH);
      digitalWrite(LED_R, HIGH);
      delay(100);
      digitalWrite(LED_B, LOW);
      digitalWrite(LED_R, LOW);
      delay(100);
    }
      winMelody();
    }
    else {
      fail(gameMode);
    }
    pinMode(WIRE, INPUT);
    attachInterrupt(WIRE, wireFunction, CHANGE);
  }

  if (accInterrupt) {
    accInterrupt = false;
    char intType = readBMA2XX(BMAREG_INTSTAT0); //Read the Interrupt Reason
    if (bitRead(intType, INTSTAT0_FLATINT) && gameMode == 2) {   //Oriantation changed
      sendPackage(11, 0);
      fail(gameMode);
    }
    else if (bitRead(intType, INTSTAT0_SLOPEINT)) { //Fast motion Interrupt
      sendPackage(12, 0);
      //fail();
    }
    else if (bitRead(intType, INTSTAT0_SLO_NO_MOT_INT) && gameMode > 0) { //Slow Motion interrupt
      sendPackage(13, 0);
      fail(gameMode);
    }
  }

  //enableComparator();
  interrupts();//reenable interrupts
  //go back to sleep
  suspend();
}

void selfTest() {
  uint16_t batVoltage = readBat();

  #ifdef LED
  analogWrite(LED_R, constrain(map(batVoltage, 490, 615, 255, 0), 0, 255)); //display the Battery voltage (1,1V - 1,5V ^= Red - Green)
  analogWrite(LED_G, constrain(map(batVoltage, 490, 615, 0, 255), 0, 255));
  delay(1000);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
    
  for(int j=0; j<512; j++) { //test LED's
    if (j % 512 < 256)
      analogWrite(LED_R, j % 256);
    else
      analogWrite(LED_R, 256 - j % 256);
  delay(1);
  }
  digitalWrite(LED_R, LOW);
  for(int j=0; j<512; j++) {
    if (j % 512 < 256)
      analogWrite(LED_G, j % 256);
    else
      analogWrite(LED_G, 256 - j % 256);
  delay(1);
  digitalWrite(LED_G, LOW);
  }
  for(int j=0; j<512; j++) {
    if (j % 512 < 256)
      analogWrite(LED_B, j % 256);
    else
      analogWrite(LED_B, 256 - j % 256);
  delay(1);
  }
  digitalWrite(LED_B, LOW);
  delay(500);
  #endif

  #ifdef BUZZER //test Buzzer
  tone(BUZZ, 2200);
  delay(100);
  tone(BUZZ, 2700);
  delay(100);
  tone(BUZZ, 3200);
  delay(100);
  noTone(BUZZ);
  delay(500);
  #endif
  
  delay(500);

  sendPackage(1, 0); //test Radio 
  
  digitalWrite(LED_G, HIGH);
  delay(200);
  digitalWrite(LED_G, LOW);
}

void deepSleep() {
  detachInterrupt(WIRE);
  detachInterrupt(INT_BMA);
  #ifdef BUZZER
  shutdownMelody(); //Windows XP shutdown melody ;)
  #endif
  #ifdef RADIO
  radio.Sleep();
  #endif
  #ifdef ACC
  writeBMA2XX(BMAREG_SLEEP_DURATION, 0b10 << BMA_LOWPOWER_ENA); //go to suspend mode (~2µA)
  #endif
  interrupts(); //enable interrupts to capture button press
  //_BIS_SR(LPM4_bits + GIE);   //low power mode
  suspend();
}

/*
void Wheel(byte WheelPos, byte pdata[]) { //HSV color table thingy
  if(WheelPos < 85) {
    pdata[0] = WheelPos * 3;
    pdata[1] = 255 - WheelPos * 3;
    pdata[2] = 0;
   return;
  } else if(WheelPos < 170) {
   WheelPos -= 85;
    pdata[0] = 255 - WheelPos * 3;
    pdata[1] = 0;
    pdata[2] = WheelPos * 3;
   return;
  } else {
   WheelPos -= 170;
   pdata[0] = 0;
   pdata[1] = WheelPos * 3;
   pdata[2] = 255 - WheelPos * 3;
   return;
  }
}*/

void fail(byte color) { //fail sound...

  #ifdef ACC
  //detachInterrupt(INT_BMA);
  delay(5);
  #endif
  for (byte i = 0; i < 5; i++) {
    if (color == RED)
    digitalWrite(LED_R, HIGH);
    else if (color == GREEN)
    digitalWrite(LED_G, HIGH);
    else if (color == BLUE)
    digitalWrite(LED_B, HIGH);
    #ifdef BUZZER
    tone(BUZZ, 3200);
    #endif
    delay(100);
    if (color == RED)
    digitalWrite(LED_R, LOW);
    if (color == GREEN)
    digitalWrite(LED_G, LOW);
    if (color == BLUE)
    digitalWrite(LED_B, LOW);
    noTone(BUZZ);
    delay(100);
  } 
  #ifdef ACC
  delay(5);
  //attachInterrupt(INT_BMA, accFunction, FALLING);
  #endif
}

void wireFunction() //ISR
{
  detachInterrupt(WIRE);
  wakeup();
  noInterrupts();
  wireInterrupt = true;
}

void buttonFunction() //ISR
{
  wakeup();
  noInterrupts();
  buttonInterrupt = true;
}

void accFunction() //ISR
{
  wakeup();
  noInterrupts();
  accInterrupt = true;
}

void startMelody() {}

#define WINSLEEP 600

void winMelody() {
  tone(BUZZ, 196);
  delay(WINSLEEP/4);
  tone(BUZZ, 262);
  delay(WINSLEEP/4);
  tone(BUZZ, 330);
  delay(WINSLEEP/4);
  tone(BUZZ, 392);
  delay(WINSLEEP/4);
  tone(BUZZ, 523);
  delay(WINSLEEP/4);

  tone(BUZZ, 784);
  delay(WINSLEEP/2);
  tone(BUZZ, 659);
  delay(WINSLEEP/2);

  // put your setup code here, to run once:
  tone(BUZZ, 207);
  delay(WINSLEEP/4);
  tone(BUZZ, 261);
  delay(WINSLEEP/4);
  tone(BUZZ, 311);
  delay(WINSLEEP/4);
  tone(BUZZ, 415);
  delay(WINSLEEP/4);
  tone(BUZZ, 523);
  delay(WINSLEEP/4);
  tone(BUZZ, 622);
  delay(WINSLEEP/4);

  tone(BUZZ, 830);
  delay(WINSLEEP/2);
  tone(BUZZ, 659);
  delay(WINSLEEP/2);

  // put your setup code here, to run once:
  tone(BUZZ, 233);
  delay(WINSLEEP/4);
  tone(BUZZ, 293);
  delay(WINSLEEP/4);
  tone(BUZZ, 349);
  delay(WINSLEEP/4);
  tone(BUZZ, 466);
  delay(WINSLEEP/4);
  tone(BUZZ, 587);
  delay(WINSLEEP/4);
  tone(BUZZ, 698);
  delay(WINSLEEP/4);

  tone(BUZZ, 932);
  delay(WINSLEEP/2);
  noTone(BUZZ);
  delay(WINSLEEP/32);
  tone(BUZZ, 988);
  delay(WINSLEEP/4);
  noTone(BUZZ);
  delay(WINSLEEP/32);
  tone(BUZZ, 988);
  delay(WINSLEEP/4);
  noTone(BUZZ);
  delay(WINSLEEP/32);
  tone(BUZZ, 988);
  delay(WINSLEEP/4);
  noTone(BUZZ);
  delay(WINSLEEP/32);

  tone(BUZZ, 1046);
  delay(WINSLEEP);
  noTone(BUZZ);
}
/*
__attribute__((interrupt(COMPARATORA_VECTOR))) //ISR
void ComparatorISR(void)
{ 
  wakeup();
  noInterrupts();
  wireInterrupt = true;
}

void enableComparator() { //TODO
  //WDTCTL = WDTPW + WDTHOLD; 
  CACTL2 = CAF + P2CA0; //no short, CA0 on -, digital Filter

  CACTL1 = CAON + CAREF_3 + CAIE + CAIES; //comparator enabled, internal diode reference, comparator interrupt enabled, rising edge interrupt
}
*/
uint16_t readBat() {
  for (byte i = 0; i < 3; i++) { //read multiple times for better Accuracy, espacially after deep sleep
    batVoltage = analogRead(ADC);
    delay(5);
  }  

  if (batVoltage < 490) {
    errorBlink(4);
    deepSleep();
  }
  return batVoltage;
}

void sendPackage(byte reason, uint16_t freq) { //package handler
  uint8_t bat = constrain(map(readBat(), 490, 615, 0, 99), 0, 99);
  #ifdef RADIO
  snprintf(payload, 28, "ID:%02d;INT:%02d;BAT:%02d;F:%03d;", NODEID, reason, bat, freq);
  radio.Wakeup();
  radio.Send(GATEWAYID, payload, strlen(payload) + 1, requestACK);
  memset(payload, 0, sizeof(payload));
  #if requestACK
  if (waitForAck()) Serial.print("ok!");
  else {
    errorBlink(3);
  }
  #endif
  #endif
}

void errorBlink(byte code) { //error blink sequence
  for (byte i = 0; i < code; i++) {
    digitalWrite(LED_R, HIGH);
    #ifdef BUZZER
    tone(BUZZ, 3200);
    #endif
    delay(100);
    digitalWrite(LED_R, LOW);
    noTone(BUZZ);
    delay(100);
  }
}

void shutdownMelody() { 
  tone(BUZZ, 1661);
  delay(300);
  tone(BUZZ, 1244);
  delay(300);
  tone(BUZZ, 830);
  delay(300);
  tone(BUZZ, 932);
  delay(300);
  noTone(BUZZ);
}

void bootMelody() {
  tone(BUZZ, 1661);
  delay(225);
  tone(BUZZ, 622);
  delay(150);
  tone(BUZZ, 932);
  delay(300);
  tone(BUZZ, 830);
  delay(450);
  tone(BUZZ, 1661);
  delay(300);
  tone(BUZZ, 932);
  delay(600);
  noTone(BUZZ);  
}

#if requestACK
// wait a few milliseconds for proper ACK, return true if received
static bool waitForAck() {
  long now = millis();
  while (millis() - now <= ACK_TIME)
    if (radio.ACKReceived(GATEWAYID))
      return true;
  return false;
}
#endif

void bma2XXclearInterrupts()
{
  // clear interrupt enable flags
  writeBMA2XX(BMAREG_DETECT_OPTS1, 0x00);
  writeBMA2XX(BMAREG_DETECT_OPTS2, 0x00);
  // clear mapping
  writeBMA2XX(BMAREG_INT_MAP1, 0x00);
  writeBMA2XX(BMAREG_INT_MAP2, 0x00);
  writeBMA2XX(BMAREG_INT_MAP3, 0x00);
  // set pins tri-state 
  writeBMA2XX(BMAREG_INTPIN_OPTS, (INTPIN_ACTIVE_LO<<INTPIN_INT1_ACTLVL) | (INTPIN_OPENDRIVE<<INTPIN_INT1_DRV) | // make pins active low and open-collector ~ tri-state
                     (INTPIN_ACTIVE_LO<<INTPIN_INT2_ACTLVL) | (INTPIN_OPENDRIVE<<INTPIN_INT2_DRV) ); 
                   
  writeBMA2XX(BMAREG_INT_CTRL_LATCH, 1 << BMA_RESET_INT); // setting this clears any latched interrupts  
}

void bma2XXsetProfile()
{

  writeBMA2XX(BMAREG_SLEEP_DURATION, 0x00);// deactivate sleep mode
  delayMicroseconds(500); //SUSPEND/LPM1: a > 450us delay is required between writeBMA2XX transactions
  bma2XXclearInterrupts();
  
  //perform a soft reset, wait >30ms
  writeBMA2XX(BMAREG_SOFTRESET, BMA_SOFTRESET_MAGICNUMBER);
  delay(50); //wait for soft reset to complete
  
  writeBMA2XX(BMAREG_BANDWIDTH, BW_500Hz);     //500 Hz BW, 1000 samples per second
  writeBMA2XX(BMAREG_ACC_RANGE, ACC_2g);
  
  writeBMA2XX(BMAREG_INT_CTRL_LATCH, 0b1011); //set interrupt resetting behavior: temporary 1ms (active low)
  
  writeBMA2XX(BMAREG_DETECT_OPTS1, (1<<DO_FLAT_EN));//(1<<DO_SLOPE_Z_EN) | (1<<DO_SLOPE_Y_EN) | (1<<DO_SLOPE_X_EN) | (1<<DO_FLAT_EN)); //enable Slope and Flat Interrupt
  writeBMA2XX(BMAREG_SLOPE_THRESHOLD, (char)(15)); //configure slope detection: ds 4.8.5
  writeBMA2XX(BMAREG_SLOW_THRESHOLD, (char)(10)); //configure slow motion detection
  writeBMA2XX(BMAREG_SLOPE_DURATION, 0b00001011); //configure slope detection: ds 4.8.5
  
  writeBMA2XX(BMAREG_DETECT_OPTS3, (1<<DO_SLO_NO_MOT_SEL) | (1<<DO_SLO_NO_MOT_Z_EN)|(1<<DO_SLO_NO_MOT_X_EN)|(1<<DO_SLO_NO_MOT_Y_EN)); //no motion on all axis
  
  writeBMA2XX(BMAREG_INT_MAP1, 1<<MAP_INT1_FLAT | 1<<MAP_INT1_NO_MOTION | 1<<MAP_INT1_SLOPE);  // ap FLAT and No motion interrupt to INT1 pin
  writeBMA2XX(BMAREG_INTPIN_OPTS, (INTPIN_ACTIVE_LO<<INTPIN_INT1_ACTLVL) | (INTPIN_PUSHPULL<<INTPIN_INT1_DRV) |
                     (INTPIN_ACTIVE_LO<<INTPIN_INT2_ACTLVL) | (INTPIN_PUSHPULL<<INTPIN_INT2_DRV) ); //pins are active low and push-pull mode      

  writeBMA2XX(BMAREG_SLEEP_DURATION, 1 << BMA_LOWPOWER_ENA); //go to low power Mode
}

char readBMA2XX(uint8_t address)
{
  //returns the contents of any 1 byte register from any address
  char buf;

  START;
  SPI.transfer(address|READ);
  buf = SPI.transfer(0xFF);
  STOP;
  return buf;
}



void writeBMA2XX(uint8_t address, char data)
{
  //write any data byte to any single address
  START;
  SPI.transfer(address);
  SPI.transfer(data);
  STOP;

  delayMicroseconds(2);
}

