#include <SoftwareSerial.h>

#define MP3_RX_PIN 10 // to chip TX
#define MP3_TX_PIN 11 // to chip RX
#define BUSY_PIN 2
#define LED_PIN 13

SoftwareSerial mp3(MP3_RX_PIN, MP3_TX_PIN);

static void sendCommand(uint8_t cmd, uint8_t param1, uint8_t param2) {
  uint8_t frame[10];
  frame[0] = 0x7E;
  frame[1] = 0xFF;
  frame[2] = 0x06;
  frame[3] = cmd;
  frame[4] = 0x00; // no ACK requested
  frame[5] = param1;
  frame[6] = param2;

  uint16_t sum = 0;
  for (uint8_t i = 1; i <= 6; i++) sum += frame[i];
  uint16_t checksum = 0 - sum;
  frame[7] = checksum >> 8;
  frame[8] = checksum & 0xFF;
  frame[9] = 0xEF;

  mp3.write(frame, sizeof(frame));
}

void setup() {
  Serial.begin(115200);
  mp3.begin(9600);
  pinMode(BUSY_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  delay(500); // let the module finish its power-on "online" announcement
  sendCommand(0x06, 0x00, 20); // set volume to 20
  sendCommand(0x03, 0x00, 1);  // play track 1
}

void loop() {
  digitalWrite(LED_PIN, digitalRead(BUSY_PIN) == LOW ? HIGH : LOW);

  while (mp3.available()) {
    Serial.print(mp3.read(), HEX);
    Serial.print(' ');
  }
}
