#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Keypad.h>
#include <Preferences.h>

// -------------------- TFT pins --------------------
// D2 is used by keypad column 3, so TFT reset must be
// connected to 3.3V and set to -1 here.
#define TFT_SCLK 9
#define TFT_MOSI 10
#define TFT_RST  -1
#define TFT_DC   7
#define TFT_CS   8
#define BUZZER   6

// -------------------- Keypad --------------------
const byte ROWS = 3;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'}
};

byte rowPins[ROWS] = {3, 4, 5};
byte colPins[COLS] = {0, 1, 2};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// -------------------- TFT class --------------------
class MyST7789 : public Adafruit_ST7789 {
public:
  MyST7789(
    int8_t cs,
    int8_t dc,
    int8_t mosi,
    int8_t sclk,
    int8_t rst
  ) : Adafruit_ST7789(cs, dc, mosi, sclk, rst) {}

  void setOffsets(uint8_t col, uint8_t row) {
    _colstart = _colstart2 = col;
    _rowstart = _rowstart2 = row;
  }
};

MyST7789 tft(
  TFT_CS,
  TFT_DC,
  TFT_MOSI,
  TFT_SCLK,
  TFT_RST
);

// -------------------- Alarm data --------------------
struct Alarm {
  byte hour;
  byte minute;
  bool enabled;
};

Alarm alarms[5] = {
  {7, 0, true},
  {8, 0, true},
  {9, 0, true},
  {12, 0, true},
  {18, 0, true}
};

// -------------------- Clock --------------------
byte currentHour = 12;
byte currentMinute = 0;
byte currentSecond = 0;

unsigned long lastClockUpdate = 0;
unsigned long lastScreenUpdate = 0;

// -------------------- Password --------------------
Preferences preferences;

char password[5] = "1593";
char enteredPassword[5] = "";
byte passwordLength = 0;

// -------------------- System status --------------------
bool alarmSystemEnabled = true;
bool buzzerActive = false;
byte editingAlarm = 255;

// -------------------- Menus --------------------
enum MenuMode {
  MENU_NONE,
  MENU_CLOCK,
  MENU_PASSWORD_OLD,
  MENU_PASSWORD_NEW
};

MenuMode menuMode = MENU_NONE;

char menuEntry[7] = "";
byte menuEntryLength = 0;

char pendingMenuKey = '\0';
unsigned long pendingMenuKeyTime = 0;
const unsigned long doublePressWindow = 800;

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);

  loadPassword();

  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  tft.init(76, 284);
  tft.setOffsets(82, 18);
  tft.invertDisplay(false);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  lastClockUpdate = millis();

  drawScreen();

  Serial.println("Alarm system ready");
  Serial.println("Press 1 twice to set the clock");
  Serial.println("Press 2 twice to change the password");
}

// -------------------- Main loop --------------------
void loop() {
  updateClock();
  checkAlarms();
  readKeypad();

  if (millis() - lastScreenUpdate >= 500) {
    lastScreenUpdate = millis();
    drawScreen();
  }
}

// -------------------- Password storage --------------------
void loadPassword() {
  preferences.begin("alarm", false);

  String savedPassword = preferences.getString(
    "password",
    "1593"
  );

  if (savedPassword.length() == 4) {
    savedPassword.toCharArray(password, sizeof(password));
  }

  preferences.end();
}

void savePassword() {
  preferences.begin("alarm", false);
  preferences.putString("password", password);
  preferences.end();
}

// -------------------- Clock functions --------------------
void updateClock() {
  if (millis() - lastClockUpdate < 1000) {
    return;
  }

  lastClockUpdate += 1000;
  currentSecond++;

  if (currentSecond >= 60) {
    currentSecond = 0;
    currentMinute++;
  }

  if (currentMinute >= 60) {
    currentMinute = 0;
    currentHour++;
  }

  if (currentHour >= 24) {
    currentHour = 0;
  }
}

// -------------------- Alarm functions --------------------
void checkAlarms() {
  if (!alarmSystemEnabled) {
    return;
  }

  if (buzzerActive) {
    return;
  }

  if (currentSecond != 0) {
    return;
  }

  for (byte i = 0; i < 5; i++) {
    if (alarms[i].enabled &&
        alarms[i].hour == currentHour &&
        alarms[i].minute == currentMinute) {
      buzzerActive = true;
      tone(BUZZER, 2200);

      passwordLength = 0;
      enteredPassword[0] = '\0';
    }
  }
}

// -------------------- Keypad functions --------------------
void readKeypad() {
  char key = keypad.getKey();

  if (key == NO_KEY) {
    if (pendingMenuKey != '\0' &&
        millis() - pendingMenuKeyTime > doublePressWindow) {
      processNormalKey(pendingMenuKey);
      pendingMenuKey = '\0';
    }

    return;
  }

  if (menuMode != MENU_NONE) {
    handleMenuKey(key);
    return;
  }

  // Press 1 twice to open the clock menu.
  // Press 2 twice to open the password menu.
  if (key == '1' || key == '2') {
    if (pendingMenuKey == key &&
        millis() - pendingMenuKeyTime <= doublePressWindow) {
      if (key == '1') {
        enterClockMenu();
      } else {
        enterPasswordMenu();
      }

      pendingMenuKey = '\0';
      return;
    }

    if (pendingMenuKey != '\0') {
      processNormalKey(pendingMenuKey);
    }

    pendingMenuKey = key;
    pendingMenuKeyTime = millis();
    return;
  }

  if (pendingMenuKey != '\0') {
    processNormalKey(pendingMenuKey);
    pendingMenuKey = '\0';
  }

  processNormalKey(key);
}

void processNormalKey(char key) {
  if (buzzerActive) {
    handleAlarmPassword(key);
    return;
  }

  if (key == '9') {
    alarmSystemEnabled = !alarmSystemEnabled;

    if (!alarmSystemEnabled) {
      noTone(BUZZER);
      buzzerActive = false;
    }

    return;
  }

  if (editingAlarm != 255) {
    editAlarm(key);
    return;
  }

  if (key >= '1' && key <= '5') {
    editingAlarm = key - '1';
    return;
  }
}

// -------------------- Alarm password --------------------
void handleAlarmPassword(char key) {
  if (passwordLength >= 4) {
    return;
  }

  enteredPassword[passwordLength++] = key;
  enteredPassword[passwordLength] = '\0';

  if (passwordLength == 4) {
    if (strcmp(enteredPassword, password) == 0) {
      noTone(BUZZER);
      buzzerActive = false;
    }

    passwordLength = 0;
    enteredPassword[0] = '\0';
  }
}

// -------------------- Alarm editing --------------------
void editAlarm(char key) {
  Alarm &alarm = alarms[editingAlarm];

  switch (key) {
    case '6':
      alarm.hour = (alarm.hour + 1) % 24;
      break;

    case '7':
      alarm.hour = (alarm.hour + 23) % 24;
      break;

    case '8':
      alarm.minute += 5;

      if (alarm.minute >= 60) {
        alarm.minute = 0;
        alarm.hour = (alarm.hour + 1) % 24;
      }

      break;

    case '9':
      if (alarm.minute < 5) {
        alarm.minute = 55;
        alarm.hour = (alarm.hour + 23) % 24;
      } else {
        alarm.minute -= 5;
      }

      break;

    case '5':
      editingAlarm = 255;
      break;
  }
}

// -------------------- Menu functions --------------------
void clearMenuEntry() {
  menuEntryLength = 0;
  menuEntry[0] = '\0';
}

char convertMenuKey(char key) {
  // In menus, keypad 9 represents zero.
  // This allows times such as 09:05:00 to be entered.
  if (key == '9') {
    return '0';
  }

  return key;
}

void enterClockMenu() {
  menuMode = MENU_CLOCK;
  clearMenuEntry();

  Serial.println();
  Serial.println("CLOCK SETTING");
  Serial.println("Enter HHMMSS");
  Serial.println("Use 9 for zero");
  Serial.println("Press 8 to save");
  Serial.println("Press 7 to cancel");
}

void enterPasswordMenu() {
  menuMode = MENU_PASSWORD_OLD;
  clearMenuEntry();

  Serial.println();
  Serial.println("PASSWORD CHANGE");
  Serial.println("Enter current password");
  Serial.println("Use 7 to cancel");
}

void handleMenuKey(char key) {
  if (key == '7') {
    menuMode = MENU_NONE;
    clearMenuEntry();
    Serial.println("Menu cancelled");
    return;
  }

  if (menuMode == MENU_CLOCK) {
    handleClockMenu(key);
    return;
  }

  if (menuMode == MENU_PASSWORD_OLD) {
    handleOldPasswordMenu(key);
    return;
  }

  if (menuMode == MENU_PASSWORD_NEW) {
    handleNewPasswordMenu(key);
    return;
  }
}

void handleClockMenu(char key) {
  if (key == '8') {
    if (menuEntryLength != 6) {
      Serial.println("Enter exactly six digits: HHMMSS");
      return;
    }

    byte newHour =
      (menuEntry[0] - '0') * 10 +
      (menuEntry[1] - '0');

    byte newMinute =
      (menuEntry[2] - '0') * 10 +
      (menuEntry[3] - '0');

    byte newSecond =
      (menuEntry[4] - '0') * 10 +
      (menuEntry[5] - '0');

    if (newHour > 23 ||
        newMinute > 59 ||
        newSecond > 59) {
      Serial.println("Invalid time");
      return;
    }

    currentHour = newHour;
    currentMinute = newMinute;
    currentSecond = newSecond;
    lastClockUpdate = millis();

    menuMode = MENU_NONE;
    clearMenuEntry();

    Serial.println("Clock saved");
    return;
  }

  if (key >= '1' &&
      key <= '9' &&
      menuEntryLength < 6) {
    menuEntry[menuEntryLength++] = convertMenuKey(key);
    menuEntry[menuEntryLength] = '\0';
  }
}

void handleOldPasswordMenu(char key) {
  if (key >= '1' &&
      key <= '9' &&
      menuEntryLength < 4) {
    menuEntry[menuEntryLength++] = key;
    menuEntry[menuEntryLength] = '\0';
  }

  if (menuEntryLength == 4) {
    if (strcmp(menuEntry, password) == 0) {
      menuMode = MENU_PASSWORD_NEW;
      clearMenuEntry();

      Serial.println("Enter new four-digit password");
    } else {
      clearMenuEntry();

      Serial.println("Wrong password");
      Serial.println("Try again");
    }
  }
}

void handleNewPasswordMenu(char key) {
  if (key >= '1' &&
      key <= '9' &&
      menuEntryLength < 4) {
    menuEntry[menuEntryLength++] = key;
    menuEntry[menuEntryLength] = '\0';
  }

  if (menuEntryLength == 4) {
    strcpy(password, menuEntry);
    savePassword();

    menuMode = MENU_NONE;
    clearMenuEntry();

    Serial.println("New password saved");
  }
}

// -------------------- Display --------------------
void drawScreen() {
  char buffer[32];

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 8);

  sprintf(
    buffer,
    "%02d:%02d:%02d",
    currentHour,
    currentMinute,
    currentSecond
  );

  tft.print(buffer);

  tft.setTextSize(2);
  tft.setCursor(8, 55);

  if (alarmSystemEnabled) {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("ALARMS ON");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.print("ALARMS OFF");
  }

  if (buzzerActive) {
    tft.setCursor(8, 80);
    tft.setTextColor(ST77XX_RED);
    tft.print("ENTER PASSWORD");
  }

  if (menuMode == MENU_CLOCK) {
    tft.setCursor(170, 55);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("CLOCK");
  }

  if (menuMode == MENU_PASSWORD_OLD ||
      menuMode == MENU_PASSWORD_NEW) {
    tft.setCursor(170, 55);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("PASS");
  }

  tft.setTextColor(ST77XX_WHITE);

  for (byte i = 0; i < 5; i++) {
    tft.setCursor(8, 112 + i * 25);

    sprintf(
      buffer,
      "A%d  %02d:%02d",
      i + 1,
      alarms[i].hour,
      alarms[i].minute
    );

    tft.print(buffer);
  }

  if (editingAlarm != 255) {
    tft.setCursor(170, 112);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("EDIT");
  }
} 