/* Copyright (C) 2016 Sean D'Epagnier <seandepagnier@gmail.com>
 *
 * This Program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 */


/*
This program is meant to interface with pwm based
motor controller either brushless or brushed, or
a regular RC servo

adc pin0 is a 1:4 resistor divider to input voltage
             allowing up to 20 volts (5 volt reference)
             
adc pin1 goes to .1 ohm shunt

pwm output on arduino pin 9


The program uses a simple protocol to ensure only
correct data can be received and to ensure that
false/incorrect or random data is very unlikely to
produce motor movement.

The input and output over uart has 3 byte packets
with 7 packets per frame.

The first two bytes are a value, and the third is
the crc of the sync byte plus the data bytes
The sync is fixed depending on the position in the frame.

If all the packets have correct crc for a few frames
the command can be recognized.

The first packet in each frame is either max current
setting (input) or voltage in the upper 12 bits, and
flags in the lower 4 bits (output)

The remaining 6 packets contain motor command (input)
or motor current (output)

*/


////// CRC
#include <avr/pgmspace.h>

const unsigned char crc8_table[256] PROGMEM
= {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

uint8_t crc8_byte(uint8_t old_crc, uint8_t byte){
    return pgm_read_byte(&crc8_table[old_crc ^ byte]);
}

uint8_t crc8_with_init(uint8_t init_value, uint8_t *pcBlock, uint8_t len)
{
    uint8_t crc = init_value;
    while (len--)
        crc = crc8_byte(crc, *pcBlock++);
    return crc;
}

uint8_t crc8(uint8_t *pcBlock, uint8_t len) {
    return crc8_with_init(0xFF, pcBlock, len);
}

////// ENDCRC

#include <avr/wdt.h>
#include <avr/sleep.h>

//#define ARDUINO_SERVO

#ifdef ARDUINO_SERVO // crappy resolution software servo
#include <Servo.h>
Servo myservo;  // create servo object to control a servo 
#endif

#define fault_pin 7 // use pin 7 for optional fault
// if switches pull this pin low, the motor is disengauged
// and will be noticed by the control program

#include <stdarg.h>
void debug(char *fmt, ... ){
    char buf[128]; // resulting string limited to 128 chars
    va_list args;
    va_start (args, fmt );
    vsnprintf(buf, 128, fmt, args);
    va_end (args);
    Serial.print(buf);
}

#include <util/delay.h>

void delay_us(long us)
{
    long i;
    for(i=0; i<us; i++)
      _delay_us(1);
}

void setup() 
{ 
    // set up Serial library
    Serial.begin(115200);
            
    wdt_disable();
    set_sleep_mode(SLEEP_MODE_IDLE); // wait for serial
//    wdt_enable(WDTO_4S);

    // setup adc
    DIDR0 = 0x3f; // disable all digital io on analog pins
//    ADMUX = _BV(REFS0); // external 5v
    ADMUX = _BV(REFS0)| _BV(REFS1); // 1.1v

    ADCSRA = _BV(ADEN) | _BV(ADIE); // enable adc with interrupts 
    ADCSRA |= _BV(ADPS0) |  _BV(ADPS1) | _BV(ADPS2); // divide clock by 128

    ADCSRA |= _BV(ADSC); // start conversion

    pinMode(fault_pin, INPUT);
    digitalWrite(fault_pin, HIGH); /* enable internal pullups */
} 

uint8_t sync_bytes[] = {0xe7, 0xf9, 0xc7, 0x1e, 0xa7, 0x19, 0x1c, 0xb3};
uint8_t nsync_bytes = (sizeof sync_bytes) / (sizeof *sync_bytes);
uint8_t sync_b = 0, sync_pos = 0, sync_count = 0;
uint8_t in_bytes[3];

uint8_t out_sync_b = 0, out_sync_pos = 0;
uint8_t crcbytes[3];
uint16_t max_current = 0;

enum {SYNC=1, FAULTPIN=2, OVERCURRENT=4, ENGAUGED=8};
uint8_t flags = 0;

uint8_t timeout;

void disengauge()
{
    flags &= ~ENGAUGED;
#ifdef ARDUINO_SERVO
    myservo.detach();
#else
    TCCR1A=0;
    TCCR1B=0;
//    DDRB&=~_BV(PB1);
    pinMode(9, INPUT);
#endif

    TIMSK2 = 0;
}

unsigned int range = 40000;

void engauge()
{
    if(flags & ENGAUGED)
        return;

#ifdef ARDUINO_SERVO
    myservo.attach(9);
#else
    //Configure TIMER1
    TCCR1A|=_BV(COM1A1)|_BV(WGM11);        //NON Inverted PWM
    TCCR1B|=_BV(WGM13)|_BV(WGM12)|_BV(CS11); //PRESCALER=8 MODE 14(FAST PWM)
        
    ICR1=range-1;  //fPWM=50Hz (Period = 20ms Standard).

//    DDRB|=_BV(PB1);   //PWM Pins as Out
    pinMode(9, OUTPUT);
#endif

// use timer2 as timeout
    timeout = 0;
    TCCR2B = _BV(CS20) | _BV(CS21) | _BV(CS22);
    TIMSK2 = _BV(TOIE2);

    flags |= ENGAUGED;
}

void position(uint16_t value)
{
#ifdef ARDUINO_SERVO
    myservo.write(value * 9 / 100 - 12);
#else
  OCR1A = 1000 + value*17/10;
#endif
}

volatile uint32_t amptotal, volttotal;
volatile uint16_t ampcount, voltcount;

uint16_t adc_current_counter;

ISR(ADC_vect)
{
    uint16_t adcw = ADCW;
    if(adc_current_counter < 2) {
      if(adc_current_counter == 1) {
        if(voltcount < 5000) {
            // if voltage
            volttotal += adcw;
            voltcount++;
        }
        ADMUX = _BV(MUX0) | _BV(REFS0) | _BV(REFS1);
      }
    } else if(adc_current_counter > 2 && ampcount < 50000) {
        amptotal += adcw;
        ampcount ++;
    }

    if(++adc_current_counter == 10) {
        adc_current_counter=0;
//        ADMUX = _BV(REFS0); // 5v reference
        ADMUX = _BV(REFS0)| _BV(REFS1); // 1.1v
    }

    ADCSRA |= _BV(ADSC); // enable conversion
}

ISR(TIMER2_OVF_vect)
{
  if(++timeout == 60) // 1 second
      disengauge();
}


uint16_t GetAmps()
{
  cli();
  uint32_t t = amptotal, d = ampcount;
  sei();
  
  return 64*t / d;
}

// rather than flush amps, keep last half
// to ensure we always have a running average
// to compare against overcurrent
uint16_t TakeAmps()
{
  cli();
  uint32_t t = amptotal, d = ampcount;
//  amptotal = ampcount=0;
   amptotal >>= 1;
   ampcount >>= 1;
  sei();
  
  uint16_t avg = 64 * t / d;
//  return avg;
  if(d & 1) {
    int havg = avg>>7;
    cli();
    amptotal -= havg;
    sei();
  }
  return avg;
}

uint16_t TakeVolts()
{
  cli();
    uint32_t t = volttotal, d = voltcount;
    volttotal = voltcount = 0;
    sei();
    return 4*t / d;
}

void debug_amps()
{
  uint32_t amps = TakeAmps();
  uint32_t ampsout =  amps * 11000 / 1024 / 64;
  
  debug("%lu.%03lu  ", ampsout/1000, ampsout%1000);
}

void debug_volts()
{
  uint32_t volts = TakeVolts();
  uint32_t voltsout = volts * 1300 * 10560. / 10 / 560 / 4096;
  
  debug("%lu.%02lu", voltsout/100, voltsout%100);
}

uint8_t serialin;

void loop()
{
    // wait for characters
    // boot powered down, wake on data
    if(!Serial.available())
      set_sleep_mode(SLEEP_MODE_IDLE);

    // serial input
    while(Serial.available()) {
      uint8_t c = Serial.read();
      if(serialin < 6)
     //    serialin+=3; //   output at 3x input rate
        serialin++; // output at input rate
      
      switch(sync_b) {
      case 0:
          in_bytes[1] = c;
          sync_b = 1;
          break;
      case 1:
          in_bytes[2] = c;
          sync_b = 2;
          break;
      case 2:
          in_bytes[0] = sync_bytes[sync_pos];
          if(c == crc8(in_bytes, 3)) {
              if(sync_count >= 2) {
//                  wdt_reset(); // strobe watchdog
                  timeout = 0;
                  flags |= SYNC;
                  uint16_t value = in_bytes[1] | (in_bytes[2]<<8);
                  if(sync_pos == 0) {
                      max_current = value;
                  } else {
                      if(value == 0x5342) { // stop
                          position(0);
                          disengauge();
                          if (flags & (FAULTPIN | OVERCURRENT)) {
                            ;//delay(1000); // delay 1 second fault
                          flags &= ~(FAULTPIN | OVERCURRENT);
                          //flags = 0; // force resync
                          }
                      } else if(value > 2000) {
                        // unused range, invalid!!!
                        // ignored
                      } else if(!(flags & (FAULTPIN | OVERCURRENT))) {                      
                          position(value);
                          engauge();
                      }
                  }
              }
              sync_b = 0;
              
              if(++sync_pos == nsync_bytes) {
                sync_pos = 0;
                if(sync_count < 3)
                  sync_count++;
              }
          } else {
              flags &= ~SYNC;
              disengauge();
              sync_pos = sync_count = 0;
              in_bytes[1] = in_bytes[2];
              in_bytes[2] = c;
          }
          break;
      }
    }

    // test fault pin
    if(!digitalRead(fault_pin)) {
        disengauge();
        flags |= FAULTPIN;
    }
    
    // test current
    uint16_t amps = GetAmps();
    if(flags & ENGAUGED && ampcount > 0 && amps >= max_current) {
        disengauge();
        flags |= OVERCURRENT;
    }

    delay(1); // small dead time to be safe
#if 0 // enable to debug directly from serial port
//engauge();
//position(550);

    debug_volts();
    debug("\n");
    debug_amps();
    debug("\n");
    debug("flags %d\n", flags);
    delay(100);
    return;
#endif
    // match output rate to input rate
    if(serialin == 0)
      return;

    if(out_sync_pos > 0) // don't decrement for voltage
      serialin--;
      
    // output 1 byte
    switch(out_sync_b) {
    case 0:
        int16_t v;
        if(out_sync_pos == 0) {
            if(voltcount == 0)
                return;
            // voltage and flags
            v = TakeVolts();
            v <<= 4; //make space for rlags
            v |= flags;         
        } else {
            if(ampcount == 0)
                return;
            v = TakeAmps();
        }
        
        crcbytes[0] = sync_bytes[out_sync_pos];
        crcbytes[1] = v;
        crcbytes[2] = v>>8;

        Serial.write(crcbytes[1]);

        if(++out_sync_pos == nsync_bytes)
            out_sync_pos = 0;

        out_sync_b = 1;
        break;
    case 1:
        // write the high byte
        Serial.write(crcbytes[2]);
        out_sync_b = 2;
        break;

    case 2:
        // write crc of sync byte plus bytes transmitted
        Serial.write(crc8(crcbytes, 3));
        out_sync_b = 0;
        break;
    }
}

