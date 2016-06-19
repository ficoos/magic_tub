
#include <SoftwareSerial.h>

#define steps 120

#define SET(x, y) (x |=(1<<y))
#define CLR(x, y) (x &=(~(1<<y)))
#define TOG(x,y) (x^=(1<<y))            		//-+


unsigned char MAGIC_NUMBER[] = {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
float values[2][steps];
float alpha[2] = {0.5, 0.5};
int maxPos[2];
float maxVal[2];
int sensors[2] = {A0, A5};
int th[2] = {100, 100};
int tl[2] = {55, 55};
int bank[2] = {0, 0};
int inst[2] = {16, 11};
int sus[2] = {32, 10};
int next_hit[2] = {0, 0};

SoftwareSerial mySerial(2, 3);

void talkMIDI(byte cmd, byte data1, byte data2) {
  mySerial.write(cmd);
  mySerial.write(data1);
  if( (cmd & 0xF0) <= 0xB0 || (cmd & 0xF0) == 0xE0 )
    mySerial.write(data2);
}

void noteOn(byte channel, byte note, byte attack_velocity) {
 talkMIDI( (0x90 | channel), note, attack_velocity);
}

void noteOff(byte channel, byte note, byte release_velocity) {
 talkMIDI( (0x80 | channel), note, release_velocity);
}

void selectInstrument(byte channel, byte instrument) {
  talkMIDI( (0xC0 | channel), instrument, 0);
}

void setController(byte channel, byte controller, byte value) {
  talkMIDI( (0xB0 | channel), controller, value);
}

void selectBank(byte channel, int bank) {
  setController(channel, 0x00, (bank >> 7) & 0x7F);
  setController(channel, 0x20, bank & 0x7F);
}

void pitchBend(byte channel, int bend) {
  bend = 0x2000 + bend;
  talkMIDI( (0xE0 | channel), bend & 0x7F, (bend >> 7) & 0x7F);
}

void setVolume(byte channel, byte volume) {
  setController(channel, 0x07, volume);
}

void welcome() {
  talkMIDI(0x90, 17, 0);
  talkMIDI(0x91, 76, 0);
}

void setup_midi() {
  mySerial.begin(31250);
  byte resetMIDI = 4;
  pinMode(resetMIDI, OUTPUT);
  digitalWrite(resetMIDI, LOW);
  delay(100);
  digitalWrite(resetMIDI, HIGH);
  delay(100);
  selectBank(0, bank[0]);
  selectBank(1, bank[1]);
  selectInstrument(0, inst[0]);
  selectInstrument(1, inst[1]);
  welcome();
}

#define MORSE_UNIT 100
#define MORSE_INST 70
#define MORSE_NOTE 70

void dash() {
  selectBank(0, 0);
  selectInstrument(0, MORSE_INST);
  noteOn(0, MORSE_NOTE, 120);
  delay(MORSE_UNIT * 3);
  noteOff(0, MORSE_NOTE, 120);
}

void dot() {
  selectBank(0, 0);
  selectInstrument(0, MORSE_INST);
  noteOn(0, MORSE_NOTE, 120);
  delay(MORSE_UNIT);
  noteOff(0, MORSE_NOTE, 120);
}

void space() {
  delay(MORSE_UNIT);
}

void morse(const char *s) {
  for (const char *c = s; *c != '\0'; c++) {
    if (*c == '-')
      dash();
    else if(*c == '.')
      dot();
    else
      space();
      
    delay(MORSE_UNIT);
  }
}

void sensor_cycle() {
  int l[] = {maxPos[0], maxPos[1]};
  for (int i = 0; i < 2; i++) {
    maxPos[i] = 0;
    maxVal[i] = 0;
  }

  for (int i = 0; i < steps; i++) {
    for (int j = 0; j < 2; j++) {
      float curVal = analogRead(sensors[j]);
      CLR(TCCR1B,0);
      TCNT1 = 0;
      ICR1 = i;
      OCR1A = i/2;
      SET(TCCR1B, 0);

      values[j][i] = values[j][i] * alpha[j] + curVal * (1 - alpha[j]);  // exponential moving avg
      if (i > 15 && values[j][i] > maxVal[j]) {                              // finds the signal peak
        maxVal[j] = values[j][i];
        maxPos[j] = l[j] * alpha[j] + i * (1 - alpha[j]);
      }
    }
  }
  
  for (int i = 0; i < 2; i++) {
    int stps = steps;
    Serial.write((char*)&MAGIC_NUMBER, sizeof(MAGIC_NUMBER));
    Serial.write((char*)&i, sizeof(int));
    Serial.write((char*)&maxPos[i], sizeof(int));
    Serial.write((char*)&maxVal[i], sizeof(float));
    Serial.write((char*)&alpha[i], sizeof(float));
    Serial.write((char*)&tl[i], sizeof(int));
    Serial.write((char*)&th[i], sizeof(int));
    Serial.write((char*)&inst[i], sizeof(int));
    Serial.write((char*)&stps, sizeof(int));
    for (int j = 0; j < steps; j++) {
      Serial.write((char*)&values[i][j], sizeof(float));
    }
  }
}

#define CAL_ALPHA 0.8

void calibrate(int index, int *out) {
  int calibrate_sec = 3;
  int finish = millis() + calibrate_sec * 1000;
  while(finish > millis())
  {
    sensor_cycle();
    *out = *out * (1 - CAL_ALPHA) + CAL_ALPHA *maxPos[index];
    delay(20);
  }
}

void setup ()
{
  pinMode (9, OUTPUT);
  pinMode (8, OUTPUT);
  TCCR1A = 0b10000010;
  TCCR1B = 0b00011001;
  ICR1 = 110;
  OCR1A = 55;
  setup_midi();
  Serial.begin(115200);
//  return;
  morse(".    -");
  calibrate(0, &tl[0]);
  tl[0]+=3;
  calibrate(1, &th[1]);
  morse(".    --");
  calibrate(0, &th[0]);
  calibrate(1, &tl[1]);
  tl[1]+=3;
  morse("-.. --- -. .");
}
int vol[] = {0, 0};
int last_note[2] = {0, 0};
bool is_playing[2] = {false, false};
int a = 1;
void loop () {
  if (Serial.available()) {
    //alpha = (float)Serial.read() / 255.0f;
    int n = Serial.read();
    int t = Serial.read();
    unsigned char vo = Serial.read();
    char v = *(char *)(&vo);
    switch(t) {
    case 0:
      tl[n] += v;
      break;
    case 1:
      th[n] += v;
      break;
    case 2:
      alpha[n] += (float)(v) * 0.01;
      break;
    case 3:
      bank[n] += v;
      selectBank(n, bank[n]);
      selectInstrument(n, inst[n]);
      break;
    case 4:
      inst[n] += v;
      selectBank(n, bank[n]);
      selectInstrument(n, inst[n]);
      break;
    }
  }

  sensor_cycle();

  TOG(PORTB, 0);

  for (int i = 0; i < 2; i++)
  {
    if (maxPos[i] > tl[i]) {
      /*
      int v = map(maxPos[i], tl[i], th[i], -0x1fff, 0x1fff);
      if (v > 0x1fff)
        v = 0x1fff;
        
      if (v < -0x1fff)
        v = -0x1fff;
      */
      int v = map(maxPos[i], tl[i], th[i], 30, 60);
      if (v > 60)
        v = 60;
        
      if (v < 30)
        v = 30;
      if (!is_playing[i] || (sus[i] != -1)) {
        selectBank(i, bank[i]);
        selectInstrument(i, inst[i]);
        //noteOff(i, 40, 0);
        //noteOn(i, 40, 100);
        next_hit[i] = millis() + sus[i];
      }
      
      noteOff(i, last_note[i], 0);
      noteOn(i, v, 100);
      
      vol[i] = min(100, vol[i] + 1);
      //setVolume(i, vol[i]);
      //pitchBend(i, v);
      last_note[i] = v;
      is_playing[i] = true;
    }
    else {
      //setVolume(i, 0);
      //noteOff(i, 40, 0);
      noteOff(i, last_note[i], 0);
      vol[i] = max(0, vol[i] - 1);
      is_playing[i] = false;
    }
  }
}

/* vim: set tw=2 ts=2 sw=2 et ft=cpp: */
