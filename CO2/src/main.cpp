#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SparkFun_SCD4x_Arduino_Library.h>


#define RELAY_PIN 2 // Реле управляет окном
#define LCD_ADDR  0x27

// Пороги CO2 (ppm) по умолчанию использую датчик scd40
#define CO2_CRIT_OPEN 1500
#define CO2_CRIT_CLOSE  1000
#define CO2_OPEN  1000
#define CO2_CLOSE 500

// Пороги температуры (C)
#define TEMP_HOT  24.0
#define TEMP_COLD 19.0

// Кулдаун после переключения реле (60 сек)
#define COOLDOWN 60000

// Состояние окна
#define WIN_CLOSED  0
#define WIN_OPEN  1

// Причины открытия
#define REASON_NONE 0
#define REASON_CRITICAL_CO2 1
#define REASON_HOT  2
#define REASON_CO2  3



LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
SCD4x scd4x;

// Показания датчика
int co2 = 0;
float temperature = 0;
float humidity = 0;

// Состояние системы
int winState = WIN_CLOSED;
int openReason = REASON_NONE;
unsigned long lastSwitchTime = 0;

bool newData = false;



int evaluateReason() {
  // Критический CO2
  if (co2 > CO2_CRIT_OPEN) {
    return REASON_CRITICAL_CO2;
  }
  if (winState == WIN_OPEN && openReason == REASON_CRITICAL_CO2 && co2 > CO2_CRIT_CLOSE) {
    return REASON_CRITICAL_CO2;
  }

  // Жарко
  if (temperature > TEMP_HOT) {
    return REASON_HOT;
  }
  if (winState == WIN_OPEN && openReason == REASON_HOT && temperature > TEMP_COLD) {
    return REASON_HOT;
  }

  // Обычный CO2, но только если не холодно
  if (temperature > TEMP_COLD) {
    if (co2 > CO2_OPEN) {
      return REASON_CO2;
    }
    if (winState == WIN_OPEN && openReason == REASON_CO2 && co2 > CO2_CLOSE) {
      return REASON_CO2;
    }
  }

  return REASON_NONE;
}

void updateWindow() {
  if (millis() - lastSwitchTime < COOLDOWN) {
    return;   // кулдаун, ничего не меняем
  }

  int desired = evaluateReason();

  if (desired == REASON_NONE && winState == WIN_OPEN) {
    digitalWrite(RELAY_PIN, LOW);
    winState = WIN_CLOSED;
    openReason = REASON_NONE;
    lastSwitchTime = millis();
  }
  else if (desired != REASON_NONE && winState == WIN_CLOSED) {
    digitalWrite(RELAY_PIN, HIGH);
    winState = WIN_OPEN;
    openReason = desired;
    lastSwitchTime = millis();
  }
  else if (desired != REASON_NONE && winState == WIN_OPEN && desired != openReason) {
    openReason = desired;
  }
}

const char* reasonText(int r) {
  if (r == REASON_CRITICAL_CO2) return "CO2 critical";
  if (r == REASON_HOT)  return "hot";
  if (r == REASON_CO2)  return "CO2";
  return "fresh air";
}

void updateDisplay() {
  char line[21];
  char tempStr[8];

  lcd.clear();

  // row 1 = CO2
  snprintf(line, sizeof(line), "CO2: %d ppm", co2);
  lcd.setCursor(0, 0);
  lcd.print(line);

  // row 2 = temp + hum
  dtostrf(temperature, 0, 1, tempStr);
  snprintf(line, sizeof(line), "T: %s C   H: %d %%", tempStr, (int)humidity);
  lcd.setCursor(0, 1);
  lcd.print(line);

  // row 3 = window status
  lcd.setCursor(0, 2);
  if (winState == WIN_OPEN) {
    lcd.print("Window: OPEN");
  } else {
    lcd.print("Window: CLOSED");
  }

  // row 4 = reason
  lcd.setCursor(0, 3);
  snprintf(line, sizeof(line), "Reason: %s", reasonText(openReason));
  lcd.print(line);
}



void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // окно закрыто

  lcd.init();
  lcd.noBacklight(); // выключаем подсветку для экономии ресурса
  lcd.clear();
  lcd.print("Start...");

  Wire.begin();

  if (scd4x.begin() == false) {
    lcd.clear();
    lcd.print("SCD40 not found");
    Serial.println("SCD40 not found");
    bool backlightFlag = true;
    while (1) {
      if ( backlightFlag ) {
        lcd.backlight();
        backlightFlag = false;
      }
      else {
        lcd.noBacklight();
        backlightFlag = true;
      };
      delay(5000);
    }
  }

  scd4x.startPeriodicMeasurement();

  // Ждем первый замер (сенсор обновляется раз в 5 секунд)
  unsigned long start = millis();
  while (newData == false && millis() - start < 10000) {
    if (scd4x.readMeasurement()) {
      co2 = scd4x.getCO2();
      temperature = scd4x.getTemperature();
      humidity = scd4x.getHumidity();
      newData = true;
    }
    delay(100);
  }

  updateDisplay();
  lastSwitchTime = millis();
}

void loop() {
  if (scd4x.readMeasurement()) {
    co2 = scd4x.getCO2();
    temperature = scd4x.getTemperature();
    humidity = scd4x.getHumidity();
    newData = true;
  }

  if (newData) {
    newData = false;
    updateWindow();
    updateDisplay();
  }
}
