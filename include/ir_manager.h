#pragma once
#include "config.h"
#include <IRac.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
class IRManager {
  IRsend sender_{IR_TX_PIN}; IRac ac_{IR_TX_PIN};
  IRrecv* receiver_=nullptr;
  uint16_t on_[512]{}, off_[512]{}, capture_[512]{};
  uint16_t onLen_=0,offLen_=0,captureLen_=0;
  void readSlot(const char* path,uint16_t* data,uint16_t& len);
 public:
  String capturedProtocol="none", lastCommand="none", lastError;
  uint32_t lastAt=0; bool known=false,on=false;
  void begin();
  bool automaticReady() const;
  bool slotsDistinct() const;
  uint32_t fingerprint() const;
  bool send(bool desired,bool diagnostic=false,bool physicalConfirmed=false);
  void poll();
  bool saveCapture(bool onSlot);
  void captureJson(JsonObject o) const;
};
extern IRManager infrared;
