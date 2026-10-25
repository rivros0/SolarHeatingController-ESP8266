#include "ir_manager.h"
IRManager infrared;
static uint32_t rawChecksum(const uint16_t* data,uint16_t len) {
  uint32_t hash=2166136261u;
  for(uint16_t i=0;i<len;i++) { hash=(hash^(data[i]&255))*16777619u; hash=(hash^(data[i]>>8))*16777619u; }
  return hash;
}
void IRManager::readSlot(const char* path,uint16_t* data,uint16_t& len) {
  File f=LittleFS.open(path,"r"); if(!f) return;
  uint32_t magic=0,checksum=0; uint16_t count=0;
  f.read(reinterpret_cast<uint8_t*>(&magic),4); f.read(reinterpret_cast<uint8_t*>(&count),2);
  f.read(reinterpret_cast<uint8_t*>(&checksum),4);
  if(magic!=0x53484952 || count<4 || count>512 || f.size()!=size_t(10+count*2)) { f.close(); return; }
  size_t n=f.read(reinterpret_cast<uint8_t*>(data),count*2); f.close();
  if(n!=size_t(count*2) || rawChecksum(data,count)!=checksum) return;
  for(uint16_t i=0;i<count;i++) if(!data[i]) return;
  len=count;
}
void IRManager::begin() {
  sender_.begin();
  readSlot("/ir-on.raw",on_,onLen_); readSlot("/ir-off.raw",off_,offLen_);
  if(cfg.receiver) { receiver_=new IRrecv(IR_RX_PIN,1024,50,true); receiver_->enableIRIn(); }
}
bool IRManager::slotsDistinct() const {
  return onLen_ && offLen_ && (onLen_!=offLen_ || memcmp(on_,off_,onLen_*sizeof(uint16_t))!=0);
}
uint32_t IRManager::fingerprint() const {
  uint32_t a=rawChecksum(on_,onLen_),b=rawChecksum(off_,offLen_);
  return (a ^ ((b<<13)|(b>>19)) ^ (uint32_t(onLen_)<<16) ^ offLen_) | 1u;
}
bool IRManager::automaticReady() const {
  return cfg.protocol=="RAW" && cfg.powerKind=="discrete" && cfg.irVerified && slotsDistinct() && cfg.irFingerprint==fingerprint();
}
bool IRManager::send(bool desired,bool diagnostic,bool physicalConfirmed) {
  lastError="";
  if(lastCommand!="none" && solar::elapsed(millis(),lastAt)<2000) { lastError="Attendere 2 secondi"; return false; }
  const bool discrete=automaticReady();
  const bool testRaw = cfg.protocol=="RAW" && cfg.powerKind=="discrete" && diagnostic && physicalConfirmed;
  if(!discrete && !testRaw && (!diagnostic||!physicalConfirmed||cfg.powerKind!="toggle")) {
    lastError="IR non verificati o toggle: conferma fisica e diagnostica necessarie"; return false;
  }
  if(known && on==desired && !(diagnostic && physicalConfirmed && cfg.protocol=="RAW" && cfg.powerKind=="discrete")) { lastError="Stato gia comandato; nessuna ritrasmissione"; return false; }
  bool sent=false;
  if(receiver_) receiver_->disableIRIn();
  if(cfg.protocol=="RAW") {
    auto data=desired?on_:off_; uint16_t len=desired?onLen_:offLen_;
    if(len) { sender_.sendRaw(data,len,38); sent=true; }
  } else {
    stdAc::state_t previous,desiredState;
    IRac::initState(&desiredState); IRac::initState(&previous);
    desiredState.protocol=cfg.protocol=="KELON"?decode_type_t::KELON:decode_type_t::WHIRLPOOL_AC;
    desiredState.model=cfg.model; desiredState.power=desired; desiredState.celsius=true;
    desiredState.mode=stdAc::opmode_t::kHeat; desiredState.degrees=cfg.setpoint;
    desiredState.fanspeed=IRac::strToFanspeed(cfg.fan.c_str());
    previous=desiredState; previous.power=!desired;
    // Toggle protocols are allowed only after the operator confirms actual state.
    sent=ac_.sendAc(desiredState,&previous);
  }
  if(receiver_) receiver_->enableIRIn();
  if(!sent) { lastError="Codice RAW mancante o protocollo non supportato"; return false; }
  known=true; on=desired; lastAt=millis(); lastCommand=desired?"HEAT ON":"OFF";
  return true;
}
void IRManager::poll() {
  if(!receiver_) return;
  decode_results result;
  if(!receiver_->decode(&result)) return;
  capturedProtocol=typeToString(result.decode_type);
  captureLen_=0;
  if(!result.overflow) {
    uint16_t len=getCorrectedRawLength(&result);
    if(len>=4 && len<=512) {
      uint16_t* raw=resultToRawArray(&result);
      if(raw) { captureLen_=len; memcpy(capture_,raw,len*sizeof(uint16_t)); delete[] raw; }
    }
  }
  // A remote command invalidates command tracking. Never infer certainty from IR
  // reception: it may be aimed elsewhere, contain a toggle, or be a partial frame.
  known=false;
  receiver_->resume();
}
bool IRManager::saveCapture(bool onSlot) {
  if(!storageReady||!captureLen_) return false;
  Config next=cfg; next.irVerified=false; next.automationEnabled=false; next.irFingerprint=0;
  if(!saveConfig(next)) return false;
  cfg=next; known=false;
  File f=LittleFS.open("/ir.new","w"); if(!f) return false;
  uint32_t magic=0x53484952,checksum=rawChecksum(capture_,captureLen_);
  size_t n=f.write(reinterpret_cast<uint8_t*>(&magic),4);
  n+=f.write(reinterpret_cast<uint8_t*>(&captureLen_),2);
  n+=f.write(reinterpret_cast<uint8_t*>(&checksum),4);
  n+=f.write(reinterpret_cast<uint8_t*>(capture_),captureLen_*2);
  f.flush(); f.close();
  if(n!=size_t(10+captureLen_*2)) return false;
  const char* path=onSlot?"/ir-on.raw":"/ir-off.raw";
  // LittleFS rename replaces an existing destination atomically.
  if(!LittleFS.rename("/ir.new",path)) return false;
  auto target=onSlot?on_:off_;
  memcpy(target,capture_,captureLen_*sizeof(uint16_t));
  if(onSlot) onLen_=captureLen_; else offLen_=captureLen_;
  // Any new slot requires renewed user verification before automation.
  // Verification is persisted as revoked BEFORE replacing either slot.
  // A backup configuration cannot authorize different slots: its fingerprint differs.
  return true;
}
void IRManager::captureJson(JsonObject o) const {
  o["protocol"]=capturedProtocol; o["length"]=captureLen_;
  o["onLength"]=onLen_; o["offLength"]=offLen_;
  JsonArray a=o.createNestedArray("raw"); for(uint16_t i=0;i<captureLen_;i++) a.add(capture_[i]);
}
