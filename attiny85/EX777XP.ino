#include <avr/sleep.h>
#include <avr/interrupt.h>

const int light_pin  = 0; // 外付けLED（PWM対応ピン PB0、Pchハイサイド）
const int led_pin    = 1; // 内蔵LED（PWM対応ピン PB1、通常ロジック）
const int button_pin = 2; // タクトスイッチ PB2, GNDに落とす

const int max_value = 255;
const double C = 255.0;

volatile bool buttonInterrupt = false;
int mode = 0; // 初期はOFF

const unsigned long longPressTime = 2000; // 長押し判定 2秒
const int fadeDelay = 4;                   // フェード速度
const unsigned long autoOffTime = 7200000; // case4 自動OFF 2時間

unsigned long case4Start = 0;

//======================================================
// 外付けLED用：PWM duty を反転して出力
//======================================================
inline void writePWM(uint8_t duty) {
  duty = 255 - duty; // Pchハイサイド用に反転
  analogWrite(light_pin, duty);
}

//======================================================
// ボタン割り込みISR（処理は軽く：押下の可能性があればフラグを立てる）
//======================================================
ISR(PCINT0_vect) {
  buttonInterrupt = true;
}

void setup() {
  // PchハイサイドMOSFETは「HIGHでOFF」なので、pinMode前にOFFを出しておく
  digitalWrite(light_pin, HIGH);
  pinMode(light_pin, OUTPUT);

  pinMode(led_pin, OUTPUT);
  pinMode(button_pin, INPUT_PULLUP);

  writePWM(0);
  analogWrite(led_pin, 0);

  // ピン変化割り込みを有効化 (PB2 = PCINT2)
  GIMSK |= (1 << PCIE);
  PCMSK |= (1 << PCINT2);
  sei();

  enterSleep(); // 電源投入時はOFF
}

void loop() {
  // case4 自動OFF判定
  if (mode == 4 && millis() - case4Start >= autoOffTime) {
    mode = 0;
    writePWM(0);
    analogWrite(led_pin, 0);
    enterSleep();
  }

  // ボタンが押された場合
  if (buttonInterrupt) {
    buttonInterrupt = false;
    delay(30); // チャタリング防止

    if (digitalRead(button_pin) == LOW) {
      unsigned long pressStart = millis();

      while (digitalRead(button_pin) == LOW) {
        // 長押し判定
        if (millis() - pressStart >= longPressTime) {
          mode = 0;
          writePWM(0);
          analogWrite(led_pin, 0);
          enterSleep();
        }
      }

      unsigned long pressDuration = millis() - pressStart;
      if (pressDuration < longPressTime) {
        // 短押しでモード切替
        if (mode != 5) { // SOSモードは loop で処理
          mode = (mode + 1) % 6;

          switch (mode) {
            case 0:
              writePWM(0);
              analogWrite(led_pin, 0);
              enterSleep();
              break;
            case 1: fade(0, max_value); break;
            case 2: fade(max_value, (int)(max_value*0.75)); break;
            case 3: fade((int)(max_value*0.75), (int)(max_value*0.5)); break;
            case 4:
              fade((int)(max_value*0.5), (int)(max_value*0.25));
              case4Start = millis(); // 自動OFFタイマー開始
              break;
            case 5: break; // SOSモードは loop で処理
          }
        }
      }
    }
  }

  // SOSモードの処理
  if (mode == 5) {
    sos_mode();
    // SOS終了後 mode が0ならOFF処理
    if (mode == 0) {
      writePWM(0);
      analogWrite(led_pin, 0);
      enterSleep();
    }
  }
}

void enterSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu();
  sleep_disable();
}

void fade(int start_value, int end_value) {
  int current_value = start_value;
  while (current_value != end_value) {
    if (current_value < end_value) current_value++;
    if (current_value > end_value) current_value--;

    int duty = get_duty(current_value);
    writePWM(duty);
    analogWrite(led_pin, duty);
    delay(fadeDelay);
  }
}

// ---------- SOS モード ----------

// SOSモード（dot=0.25s, dash=0.75s, セット間隔3sOFF）
void sos_mode() {
  while (mode == 5) {
    // S = dot dot dot
    for (int i=0; i<3; i++) if (checkButtonSOS()) return; else sos_blink(250, 250);
    // O = dash dash dash
    for (int i=0; i<3; i++) if (checkButtonSOS()) return; else sos_blink(750, 250);
    // S = dot dot dot
    for (int i=0; i<3; i++) if (checkButtonSOS()) return; else sos_blink(250, 250);

    // セット間隔 3秒OFF
    writePWM(0);
    analogWrite(led_pin, 0);
    if (wait_ms_with_abort(3000)) return;
  }
}

// こま切れで待ち、途中でボタンが押されたら true を返す
bool wait_ms_with_abort(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    if (checkButtonSOS()) return true;
    delay(10); // 10ms刻みで待つ
  }
  return false;
}

// SOSの1パターン（ON→OFF）
void sos_blink(int on_ms, int off_ms) {
  writePWM(255);
  analogWrite(led_pin, 255);
  if (wait_ms_with_abort(on_ms)) return;

  writePWM(0);
  analogWrite(led_pin, 0);
  if (wait_ms_with_abort(off_ms)) return;
}

// SOS中のボタン判定（短押し/長押しどちらでもOFF）
bool checkButtonSOS() {
  if (digitalRead(button_pin) == LOW) {
    unsigned long pressStart = millis();
    while(digitalRead(button_pin) == LOW) {
      if (millis() - pressStart >= longPressTime) {
        mode = 0; // 長押しでOFF
        return true;
      }
    }
    // 短押しでもOFF
    mode = 0;
    return true;
  }
  return false;
}

// 人間の目に合わせたガンマ補正付きduty計算
int get_duty(double ratio) {
  return round(exp(log(255.0) - (1 - (ratio/255.0))*log(C)));
}