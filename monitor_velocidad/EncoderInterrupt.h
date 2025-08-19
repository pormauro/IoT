#ifndef ENCODER_INTERRUPT_H
#define ENCODER_INTERRUPT_H

#include <Arduino.h>

class EncoderInterrupt {
public:
  EncoderInterrupt(uint8_t pinA, uint8_t pinB);
  void begin();
  long read() const;
  void write(long newPos);

private:
  uint8_t pinA_;
  uint8_t pinB_;
  volatile long position_;

  void update();
  static EncoderInterrupt* instance_;
  static void handleA();
  static void handleB();
};

#endif
