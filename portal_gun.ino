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
  STATE_SONG_PLAYING,
  STATE_SONG_IDLE,
  STATE_SONG_END
};
State currentState = STATE_OFF;

unsigned long lastSoundTime = 0;
const unsigned long debounceSoundDelay = 500; // мс

bool playing = false;
bool powerPressed = false;
bool bluePressed = false;
bool orangePressed = false;
bool songPressed = false;

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
  lastSoundTime = millis();  
}

void playIdleSound() {
  uint8_t cmd1[] = {0x7E, 0x03, 0x11, 0x02, 0xEF};
  sendJQCommand(cmd1, sizeof(cmd1));
  delay(150);
  uint8_t cmd2[] = {0x7E, 0x04, 0x03, 0x00, SND_IDLE, 0xEF};
  sendJQCommand(cmd2, sizeof(cmd2));
  lastSoundTime = millis();
  idlePlaying = true;
}

void pausePlay() {
  uint8_t cmd[] = {0x7E, 0x02, 0x0E, 0xEF};
  sendJQCommand(cmd, sizeof(cmd));
  lastSoundTime = millis();
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

bool downButton(bool btn, bool &trigger, State newState) {
  if (btn && !trigger) {
    trigger = true;
    currentState = newState;
    Serial.println(currentState);
    return true;
  }
  return false;
}

void waitUpButton(bool btn, bool &trigger, uint8_t sound, State newState) {
  if (!btn && trigger) {
    trigger = false;
    if (sound == 0)
      pausePlay();
    else
      playSound(sound);
  } else if (!trigger && !playing) {  //дожидаемся завершения звука
    currentState = newState;
    Serial.println(currentState);
    idlePlaying = false;
  }
}

// сигнал Busy на плеере появляется не сразу
void updatePlaying() {
  unsigned long now = millis();
  if ((now - lastSoundTime) > debounceSoundDelay)
    playing = digitalRead(JQ_BUSY);
}

void loop() {
  updatePlaying();
  // playing = digitalRead(JQ_BUSY);

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
  bool blueOn        = stableBlue == LOW;
  bool orangeOn      = stableOrange == LOW;
  bool songOn       = stableSong == LOW;

  switch (currentState) {
    case STATE_OFF:
      downButton(powerOn, powerPressed, STATE_POWERING_UP);      
      break;

    case STATE_POWERING_UP:
      waitUpButton(powerOn, powerPressed, SND_POWER_UP, STATE_IDLE);      
      break;

    case STATE_IDLE:
      if (downButton(powerOn, powerPressed, STATE_POWERING_DOWN))
        break;
      if (downButton(blueOn, bluePressed, STATE_BLUE_FIRING))
        break;
      if (downButton(orangeOn, orangePressed, STATE_ORANGE_FIRING))
        break;
      if (downButton(songOn, songPressed, STATE_SONG_PLAYING))
        break;
      // if (!idlePlaying && !playing)
      //   playIdleSound();      
      break;

    case STATE_BLUE_FIRING:
      waitUpButton(blueOn, bluePressed, SND_BLUE_FIRE, STATE_IDLE);
      break;
      // setLightsState(1);      

    case STATE_ORANGE_FIRING:
      waitUpButton(orangeOn, orangePressed, SND_ORANGE_FIRE, STATE_IDLE);
      break;
      // setLightsState(0);            

    case STATE_SONG_PLAYING:      
      waitUpButton(songOn, songPressed, SND_SONG, STATE_SONG_IDLE);
      break;

    case STATE_SONG_IDLE:
      downButton(songOn, songPressed, STATE_SONG_END);
      break;

    case STATE_SONG_END:
      waitUpButton(songOn, songPressed, 0, STATE_IDLE);
      break;

    case STATE_POWERING_DOWN:
      waitUpButton(powerOn, powerPressed, SND_POWER_DOWN, STATE_OFF);      
      break;
  }
}
