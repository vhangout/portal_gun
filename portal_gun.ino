#include <initializer_list>
// Пины
#define BLUEBTN   14
#define ORANGEBTN 15
#define SONGBTN   16
#define POWERSW   17

#define JQ_BUSY   18
#define JQ_TX 8   // TX RP2040 → RX JQ6500
#define JQ_RX 9   // RX RP2040 ← TX JQ6500

#define SND_POWER_UP    1
#define SND_IDLE        2
#define SND_BLUE_FIRE   3
#define SND_ORANGE_FIRE 4
#define SND_CANCEL      5
#define SND_POWER_DOWN  6
#define SND_SONG        7

// Индексы пикселей
int centerStart = 1;
int centerEnd   = 14;
int ringStart   = 15;
int ringEnd     = 30;

// FSM состояния
enum State {
  STATE_OFF,
  STATE_POWERING_UP,
  STATE_IDLE,
  STATE_BLUE_FIRING,
  STATE_ORANGE_FIRING,
  STATE_POWERING_DOWN,
  STATE_SONG_PLAYING
};
State currentState = STATE_OFF;

bool songPressed = false;
bool powerPressed = false;
bool idlePlaying = false;

// === Антидребезг кнопок ===
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // мс

bool lastPowerState = HIGH, lastBlueState = HIGH, lastOrangeState = HIGH, lastSongState = HIGH;
bool stablePower = HIGH, stableBlue = HIGH, stableOrange = HIGH, stableSong = HIGH;

// === Команды JQ6500 ===
void sendJQCommand(const uint8_t *cmd, size_t len) {
  Serial2.write(cmd, len);
  Serial2.flush();
}

void jqInit() {
  // select flash
  uint8_t cmd[] = {0x7E, 0x03, 0x09, 0x04, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
  delay(150);
}

void setVolume(uint8_t vol) {
  if (vol > 30) vol = 30;
  uint8_t cmd[] = {0x7E, 0x03, 0x06, vol, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
}

void playSound(uint8_t track) {
  uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x04, 0xEF};
  sendJQCommand(cmd1, sizeof(cmd1));
  delay(150);
  uint8_t cmd[] = {0x7E, 0x04, 0x03, 0x00, track, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
  delay(1000);
}

void playIdleSound() {
  uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x02, 0xEF};
  sendJQCommand(cmd1, sizeof(cmd1));
  delay(150);
  uint8_t cmd2[] = {0x7E, 0x04, 0x03, 0x00, SND_IDLE, 0xEF};
  sendJQCommand(cmd2, sizeof(cmd2));
  delay(150);
  idlePlaying = true;
}

void setLightsState(int state) {
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1);
  
  delay(1000);

  pinMode(JQ_BUSY, INPUT);
  for (int pin : {SONGBTN, POWERSW, BLUEBTN, ORANGEBTN}) {
    pinMode(pin, INPUT_PULLUP);
  }

  jqInit();
}

void loop() {
  bool playing = digitalRead(JQ_BUSY);

  bool rawPower   = digitalRead(POWERSW);
  bool rawBlue    = digitalRead(BLUEBTN);
  bool rawOrange  = digitalRead(ORANGEBTN);
  bool rawSong    = digitalRead(SONGBTN);

  unsigned long now = millis();
  // Проверяем изменения состояний
  
  if (rawPower != lastPowerState || rawBlue != lastBlueState || rawOrange != lastOrangeState || rawSong != lastSongState) {
    lastDebounceTime = now;
  }

  // Если прошло 50 мс без изменений — фиксируем новое стабильное состояние
  if ((now - lastDebounceTime) > debounceDelay) {
    stablePower  = rawPower;
    stableBlue   = rawBlue;
    stableOrange = rawOrange;
    stableSong   = rawSong;
  }

  // Обновляем прошлые значения
  lastPowerState = rawPower;
  lastBlueState = rawBlue;
  lastOrangeState = rawOrange;
  lastSongState = rawSong;

   // Используем только стабильные значения:
  bool powerOn       = stablePower == LOW;
  bool bluePressed   = stableBlue == LOW;
  bool orangePressed = stableOrange == LOW;
  bool songBtn       = stableSong == LOW;

  switch (currentState) {
    case STATE_OFF:
      if (powerOn && !powerPressed) {
          powerPressed = 1;
          currentState = STATE_POWERING_UP;
          Serial.println("STATE_POWERING_UP");
      }      
      break;

    case STATE_POWERING_UP:
      if (!powerOn && powerPressed) {
        powerPressed = 0;
        playSound(SND_POWER_UP);        
      } else if (!powerPressed && !playing) {  //дожидаемся завершения звука
          currentState = STATE_IDLE;
          Serial.println("STATE_IDLE");
      }
      break;

    case STATE_IDLE:
      if (powerOn && !powerPressed) {
          powerPressed = true;
          currentState = STATE_POWERING_DOWN;
          Serial.println("STATE_POWERING_DOWN");
      }
      if (bluePressed) {        
        currentState = STATE_BLUE_FIRING;
        Serial.println("STATE_BLUE_FIRING");
      } else if (orangePressed) {
        playSound(SND_ORANGE_FIRE);
        currentState = STATE_ORANGE_FIRING;
        Serial.println("STATE_ORANGE_FIRING");
      } else if (songBtn && !songPressed) {
        playSound(SND_SONG);
        songPressed = true;
        currentState = STATE_SONG_PLAYING;
        Serial.println("STATE_SONG_PLAYING");
      } else if (!idlePlaying && !playing)
        playIdleSound();      
      break;

    case STATE_BLUE_FIRING:
      setLightsState(1);
      if (!bluePressed) {
        currentState = STATE_IDLE;
        Serial.println("STATE_IDLE");
      }

      if (!powerOn && powerPressed) {
        powerPressed = 0;
        playSound(SND_POWER_UP);        
      } else if (!powerPressed && !playing) {  //дожидаемся завершения звука
          currentState = STATE_IDLE;
          Serial.println("STATE_IDLE");
      }

      break;

    case STATE_ORANGE_FIRING:
      setLightsState(0);
      if (!orangePressed) {
        currentState = STATE_IDLE;
        Serial.println("STATE_IDLE");
      }
      break;

    case STATE_SONG_PLAYING:
      if (!songBtn && songPressed) {
        songPressed = false;
        currentState = STATE_IDLE;
        Serial.println("STATE_IDLE");
      }
      break;

    case STATE_POWERING_DOWN:
      if (!powerOn && powerPressed) {
        powerPressed = false;
        playSound(SND_POWER_DOWN);
        currentState = STATE_OFF;
        Serial.println("STATE_OFF");
      }      
      break;
  }
}
