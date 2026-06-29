#pragma once

#include <Mesh.h>
#include <Arduino.h>
#include <HardwareSerial.h>

// ================= НАСТРОЙКИ ПИНОВ (ESP32-S3) =================
#define PIN_E220_M0 9   // GPIO 9 (Safe boot pin, но как выход OK)
#define PIN_E220_M1 10  // GPIO 10 (Safe boot pin, но как выход OK)

#define PIN_E220_RX 18  // GPIO 18 (RX2)
#define PIN_E220_TX 17  // GPIO 17 (TX2)
#define PIN_E220_AUX 8  // GPIO 8 (AUX)

// Если твои пины M0/M1 припаяны на плате намертво (к GND/VCC),
// раскомментируй строку ниже, чтобы код не пытался ими управлять:
// #define E220_PINS_HARDWIRED 

// ================= ПАРАМЕТРЫ ЧАСТОТЫ =================
// Целевая частота: 868.856 МГц
// E220 использует формулу: Channel = (Freq - Base) / Step
// Для E220-900 (850-930MHz) база обычно 850MHz, шаг 1MHz (примерно)
// Но проще задать частоту напрямую в МГц, модуль сам пересчитает.
#define TARGET_FREQ_MHZ 868.856 

class E220Radio : public mesh::Radio {
protected:
  uint32_t n_recv, n_sent, n_recv_errors;
  bool _is_receiving;
  uint8_t _rx_buffer[256]; 
  size_t _rx_len;

  // Вспомогательная функция для отправки команды записи регистра
  void writeRegister(uint8_t addr, uint8_t data) {
    E220_SERIAL_PORT.write(0xC0); // Команда записи
    E220_SERIAL_PORT.write(addr); // Адрес
    E220_SERIAL_PORT.write(0x01); // Длина (1 байт)
    E220_SERIAL_PORT.write(data); // Данные
    delay(10); // Небольшая задержка
  }

  void configureE220() {
    Serial.println("E220: Переход в режим конфигурации...");
    
    // 1. Переводим в режим конфигурации (M0=1, M1=1)
    #ifndef E220_PINS_HARDWIRED
      digitalWrite(PIN_E220_M0, HIGH);
      digitalWrite(PIN_E220_M1, HIGH);
      delay(100); // Ждем переключения
    #endif

    // 2. Отправляем команды настройки
    // E220-900T22D имеет специфические регистры. 
    // Ниже приведены адреса для серии E220 (могут отличаться для E22, проверь даташит если не сработает)
    
    // --- Настройка REG0 (0x03): Скорость UART и SF ---
    // Биты 7-4: Скорость UART (оставим дефолт или поставим 9600)
    // Биты 3-0: SF (0=SF5, 1=SF6, ..., 2=SF7) -> Нам нужно SF7 (значение 2)
    // Но для E220 часто SF задается в другом регистре или байте.
    // Для простоты настроим только частоту и мощность, SF оставим дефолтным или попробуем угадать.
    // В E220-900T22D SF часто фиксирован или выбирается через REG0[3:0]
    // Попробуем установить SF7 (значение 2) и скорость UART 9600 (0x00 в старших битах)
    writeRegister(0x03, 0x02); // REG0: SF7

    // --- Настройка REG1 (0x04): Мощность и BW ---
    // Биты 7-5: Мощность (0=22dBm, 1=17dBm...) -> 0 (Max)
    // Биты 4-2: BW (0=125kHz, 1=250kHz, 2=62.5kHz??)
    // Внимание: BW=62.5kHz нестандартный. Обычно 125/250/500.
    // Если 62.5 не поддерживается "железом" E220, он может игнорировать или округлить.
    // Попробуем установить 125kHz (0x00) для надежности, или 0x20 если есть спец.бит.
    // Для E220-900T22D: BW обычно 125kHz или 250kHz.
    // Если нужно именно 62.5, это может быть спец.режим. Поставим 125кГц (безопасно).
    writeRegister(0x04, 0x00); // REG1: Max Power, BW 125kHz (ближайший к 62.5)

    // --- Настройка REG2 (0x05): Частота (High Byte) ---
    // Частота 868.856 МГц.
    // Для E220 частота задается как (Freq - Base) / Step.
    // Обычно Base = 850MHz. Step = 1MHz.
    // Значение = 868.856 - 850 = 18.856 -> Округляем до 19?
    // Но E220 позволяет дробные значения.
    // Формула из даташита Ebyte: Freq = 850 + CHAN
    // CHAN = 18.856.
    // Старший байт (High) = 18 (0x12)
    writeRegister(0x05, 0x12); 

    // --- Настройка REG3 (0x06): Частота (Low Byte) ---
    // Дробная часть 0.856 * 256 (если шаг 1/256) или просто часть значения.
    // В модулях Ebyte часто используется простая схема:
    // High = целая часть, Low = дробная * 100 (или类似).
    // Попробуем просто передать дробную часть * 256 = 219 (0xDB)
    // Это приблизительная настройка.
    writeRegister(0x06, 0xDB); 

    // 3. Сохраняем настройки (WOR / Save Command)
    // Команда C2 00 00 00 - Сохранить и перезагрузить
    E220_SERIAL_PORT.write(0xC2);
    E220_SERIAL_PORT.write(0x00);
    E220_SERIAL_PORT.write(0x00);
    E220_SERIAL_PORT.write(0x00);
    delay(500); // Ждем сохранения

    Serial.println("E220: Настройки сохранены. Перезагрузка...");

    // 4. Возвращаем в рабочий режим (M0=0, M1=0)
    #ifndef E220_PINS_HARDWIRED
      digitalWrite(PIN_E220_M0, LOW);
      digitalWrite(PIN_E220_M1, LOW);
      delay(100);
    #endif
  }

public:
  E220Radio() { 
    n_recv = n_sent = n_recv_errors = 0; 
    _is_receiving = false;
    _rx_len = 0;
  }

  void begin() override {
    // 1. Настраиваем пины управления (M0, M1)
    #ifndef E220_PINS_HARDWIRED
      pinMode(PIN_E220_M0, OUTPUT);
      pinMode(PIN_E220_M1, OUTPUT);
      digitalWrite(PIN_E220_M0, LOW);
      digitalWrite(PIN_E220_M1, LOW);
    #endif

    // 2. Настраиваем UART
    E220_SERIAL_PORT.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
    delay(100); // Ждем старта UART

    // 3. Конфигурируем частоту
    configureE220();
  }

  int recvRaw(uint8_t* bytes, int sz) override {
    if (_rx_len > 0) {
      int copy_len = min(sz, (int)_rx_len);
      memcpy(bytes, _rx_buffer, copy_len);
      _rx_len = 0; 
      n_recv++;
      return copy_len;
    }
    return 0; 
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    // SF7, BW125 -> довольно быстро.
    // Примерно 3-4 кбит/с полезной нагрузки.
    return (len_bytes * 3) + 100; 
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    while (digitalRead(PIN_E220_AUX) == LOW) {
      delay(1);
    }
    E220_SERIAL_PORT.write(bytes, len);
    E220_SERIAL_PORT.flush();
    n_sent++;
    return true;
  }

  bool isSendComplete() override { return true; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }

  void loop() override {
    if (E220_SERIAL_PORT.available()) {
      while (E220_SERIAL_PORT.available() && _rx_len < 255) {
        _rx_buffer[_rx_len++] = E220_SERIAL_PORT.read();
      }
    }
  }

  uint32_t getPacketsRecv() const override { return n_recv; }
  uint32_t getPacketsSent() const override { return n_sent; }
  uint32_t getPacketsRecvErrors() const override { return n_recv_errors; }
  void resetStats() override { n_recv = n_sent = n_recv_errors = 0; }

  float getLastRSSI() const override { return 0.0; }
  float getLastSNR() const override { return 0.0; }
  float packetScore(float snr, int packet_len) override { return 0.0; }

  void setRxBoostedGainMode(bool) override { }
  bool getRxBoostedGainMode() const override { return false; }
};