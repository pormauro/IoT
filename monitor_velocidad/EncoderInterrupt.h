#pragma once
#include <Arduino.h>

/*
  EncoderInterrupt — modo 1× ultraliviano (RISING en A)
  - Interrupción SOLO en A (RISING)
  - B define el sentido
  - Lectura directa de puerto para máxima velocidad (AVR)
  - API: begin(), read(), write()

  Si el signo queda al revés, poné ENCODER_DIR_INVERT en 1.
*/

#ifndef ENCODER_DIR_INVERT
#define ENCODER_DIR_INVERT 0   // 0 = normal, 1 = invierte signo
#endif

class EncoderInterrupt {
public:
  EncoderInterrupt(uint8_t pinA, uint8_t pinB);

  void begin();
  long read() const;
  void write(long newPos);

private:
  static void handleA();
  static void handleB();           // no usado en 1× (stub)
  static EncoderInterrupt* instance_; // singleton simple (UNO: 1 encoder)

  const uint8_t pinA_;
  const uint8_t pinB_;
  volatile long position_ = 0;

  // Fast I/O (AVR). En otras arch, se cae a digitalRead() en .cpp.
  volatile uint8_t* portAin_ = nullptr;
  volatile uint8_t* portBin_ = nullptr;
  uint8_t maskA_ = 0, maskB_ = 0;
  uint8_t lastA_ = 0;

  inline void isrA();

  inline uint8_t fastReadA() const {
#if defined(ARDUINO_ARCH_AVR)
    return (*portAin_ & maskA_) ? 1 : 0;
#else
    return digitalRead(pinA_) ? 1 : 0;
#endif
  }
  inline uint8_t fastReadB() const {
#if defined(ARDUINO_ARCH_AVR)
    return (*portBin_ & maskB_) ? 1 : 0;
#else
    return digitalRead(pinB_) ? 1 : 0;
#endif
  }
};
