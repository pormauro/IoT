#include "EncoderInterrupt.h"

EncoderInterrupt* EncoderInterrupt::instance_ = nullptr;

EncoderInterrupt::EncoderInterrupt(uint8_t pinA, uint8_t pinB)
  : pinA_(pinA), pinB_(pinB), position_(0) {}

void EncoderInterrupt::begin() {
  instance_ = this;
  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinA_), handleA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB_), handleB, CHANGE);
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

void EncoderInterrupt::update() {
  bool a = digitalRead(pinA_);
  bool b = digitalRead(pinB_);
  if (a == b) {
    position_++;
  } else {
    position_--;
  }
}

void EncoderInterrupt::handleA() {
  if (instance_) {
    instance_->update();
  }
}

void EncoderInterrupt::handleB() {
  if (instance_) {
    instance_->update();
  }
}
