#include "EncoderInterrupt.h"

EncoderInterrupt* EncoderInterrupt::instance_ = nullptr;

EncoderInterrupt::EncoderInterrupt(uint8_t pinA, uint8_t pinB)
  : pinA_(pinA), pinB_(pinB) {}

void EncoderInterrupt::begin() {
  instance_ = this;

  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);

#if defined(ARDUINO_ARCH_AVR)
  // Mapear a registros/máscaras para lectura rápida
  portAin_ = portInputRegister(digitalPinToPort(pinA_));
  portBin_ = portInputRegister(digitalPinToPort(pinB_));
  maskA_   = digitalPinToBitMask(pinA_);
  maskB_   = digitalPinToBitMask(pinB_);
#endif

  lastA_ = fastReadA();

  // Modo 1×: solo flanco ascendente en A
  attachInterrupt(digitalPinToInterrupt(pinA_), handleA, RISING);
  // B no registra interrupción (1×)
}

long EncoderInterrupt::read() const {
  noInterrupts();
  long r = position_;
  interrupts();
  return r;
}

void EncoderInterrupt::write(long newPos) {
  noInterrupts();
  position_ = newPos;
  interrupts();
}

// ISR de A (RISING). B define el sentido.
inline void EncoderInterrupt::isrA() {
  uint8_t b = fastReadB();
#if ENCODER_DIR_INVERT
  position_ += (b ? +1 : -1);
#else
  position_ += (b ? -1 : +1);
#endif
  lastA_ = 1; // opcional (diagnóstico)
}

// Stub por compatibilidad (no usado en 1×)
void EncoderInterrupt::handleB() { /* no-op */ }

void EncoderInterrupt::handleA() {
  if (instance_) instance_->isrA();
}
