/*  Encoder Reporter UNO (HTTP, UDP) - W5100/W5500 (modo 1× ultrarrápido)
    NOTA: HTTPS/TLS no es posible en ATmega328P por limitaciones de memoria.

    Endpoints:
      GET  /           → HTML mínimo de configuración
      GET  /config     → HTML de configuración
      POST /config     → Guarda configuración en EEPROM (key=value)
      GET  /data       → JSON {"position":<int>,"speed":<float2>,"time":<uint>,"run":<uint>,"stop":<uint>}
      GET  /reset      → Pone a cero encoder y tiempo (204)
      GET  /send-test  → Envía JSON una vez al destino configurado (204)
      GET  /calib      → HTML para calibrar PPM
      POST /calib      → Guarda PPM (pulsos por metro)
      OPTIONS *        → Responde CORS preflight (204)

    Función periódica: envío de JSON por HTTP (POST) o UDP cada 'interval' ms.
    Encoder (1× rápido): interrupción SOLO en A (RISING); B define el sentido.
    Lectura de B por puerto directo (sin digitalRead) para máxima velocidad.

    Extras (fiabilidad):
      - Watchdog configurable (on/off, 1/2/4/8s).
      - Snapshots en EEPROM (anillo) para no perder position/run/stop tras reset.

    Compila en Arduino UNO + Ethernet (W5100/W5500).
*/

#define DEBUG 0
#ifndef ENCODER_DIR_INVERT
#define ENCODER_DIR_INVERT 0   // 0 = dirección normal; 1 = invierte (+/-)
#endif

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>
#include <avr/io.h>

// --- FIX para UNO/AVR: IRAM_ATTR no existe (solo ESP32) ---
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// ---------- Ajustes de hardware/red ----------
const uint8_t PIN_ENC_A = 2;  // INT0
const uint8_t PIN_ENC_B = 3;  // INT1
const uint8_t MAC[6]   = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};

// ---------- Servidores ----------
EthernetServer server(80);
EthernetUDP Udp;
const uint16_t LOCAL_UDP_PORT = 32100;

// ---------- Encoder (modo 1× rápido) ----------
volatile long encCount = 0;
// Punteros y máscaras para lectura directa del pin B (dirección)
volatile uint8_t* ENC_PORTB_IN = nullptr;
uint8_t ENC_MASKB = 0;

// ---------- Tiempo/velocidad ----------
unsigned long tStartMs = 0;
unsigned long lastSpdMs = 0;
long   lastSpdCount = 0;
float  lastSpeedCps = 0.0f; // cuentas/seg
unsigned long runSecs = 0;
unsigned long stopSecs = 0;

// ---------- Envío periódico ----------
unsigned long lastSendMs = 0;

// ---------- Snapshots EEPROM ----------
#define SNAP_MAGIC      0xC0FFEE55UL
#define SNAP_SLOTS      16
#define SNAP_BASE_ADDR  128
struct __attribute__((packed)) Snapshot {
  uint32_t magic;
  uint32_t seq;
  int32_t  enc;
  uint32_t run;
  uint32_t stop;
  uint8_t  crc;
};
unsigned long lastSnapMs = 0;
long lastSnapEnc = 0;
uint32_t snapSeq = 0;
const uint16_t SNAP_SEC_DEFAULT = 60;
const uint16_t SNAP_DELTA_DEFAULT = 1000;

// ---------- Utilidades pequeñas ----------
static inline void safe_strcpy(char* dst, const char* src, size_t n) {
  if (!n) return; size_t i=0; for(; i<n-1 && src && src[i]; ++i) dst[i]=src[i]; dst[i]='\0';
}
static bool parseIP(const char* s, uint8_t out[4]) {
  if(!s) return false; uint16_t acc=0; uint8_t dots=0;
  for (const char* p=s; ; ++p){ char c=*p;
    if (c>='0'&&c<='9'){ acc=acc*10+(c-'0'); if(acc>255) return false; }
    else if(c=='.'||c=='\0'){ if(dots>3) return false; out[dots++]=(uint8_t)acc; acc=0; if(c=='\0') break; }
    else return false;
  }
  return dots==4;
}
static void ipToStr(const uint8_t ip[4], char* buf, size_t n) { snprintf(buf, n, "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]); }
static uint8_t crc8(const uint8_t* p, size_t n){ uint8_t c=0; while(n--) c^=*p++; return c; }

#if DEBUG
#define DPRINTLN(x) Serial.println(x)
#define DPRINT(x)   Serial.print(x)
#else
#define DPRINTLN(x)
#define DPRINT(x)
#endif

// ---------- Configuración persistente ----------
struct Config {
  uint32_t magic;      // 0xC0FFEE01
  uint8_t  version;    // 2
  uint8_t  dhcp;       // 0/1
  uint8_t  ip[4];
  uint8_t  gw[4];
  uint8_t  sm[4];
  uint8_t  dns[4];
  uint8_t  enabled;    // envío periódico
  uint8_t  proto;      // 0=http, 1=udp
  uint8_t  host[4];    // IP destino
  uint16_t port;       // destino
  char     path[32];   // HTTP POST path
  uint32_t intervalMs; // intervalo envío
  uint32_t ppm;        // pulsos por metro
  // Fiabilidad
  uint8_t  wdt_en;     // 0=off,1=on
  uint8_t  wdt_to;     // 1,2,4,8 (segundos)
  uint16_t snapSec;    // cada N s snapshot
  uint16_t snapDelta;  // o si cambia N cuentas
} cfg;

const uint32_t CFG_MAGIC = 0xC0FFEE01UL;
const int EEPROM_ADDR = 0;

void setDefaults() {
  cfg.magic = CFG_MAGIC; cfg.version = 2; cfg.dhcp = 1;
  cfg.ip[0]=192; cfg.ip[1]=168; cfg.ip[2]=1; cfg.ip[3]=120;
  cfg.gw[0]=192; cfg.gw[1]=168; cfg.gw[2]=1; cfg.gw[3]=1;
  cfg.sm[0]=255; cfg.sm[1]=255; cfg.sm[2]=255; cfg.sm[3]=0;
  cfg.dns[0]=8; cfg.dns[1]=8; cfg.dns[2]=8; cfg.dns[3]=8;
  cfg.enabled = 0; cfg.proto = 0;
  cfg.host[0]=192; cfg.host[1]=168; cfg.host[2]=1; cfg.host[3]=10;
  cfg.port = 80; safe_strcpy(cfg.path, "/ingest", sizeof(cfg.path));
  cfg.intervalMs = 5000; cfg.ppm = 1000;
  cfg.wdt_en = 0; cfg.wdt_to = 4;
  cfg.snapSec = SNAP_SEC_DEFAULT; cfg.snapDelta = SNAP_DELTA_DEFAULT;
}
void loadConfig() {
  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != 2) { setDefaults(); EEPROM.put(EEPROM_ADDR, cfg); }
}
void saveConfig() { EEPROM.put(EEPROM_ADDR, cfg); }

// ---------- Velocidad (cps) ----------
void updateSpeedIfDue() {
  unsigned long now = millis();
  unsigned long dt = now - lastSpdMs;
  if (dt >= 1000 || lastSpdMs == 0) { // 1 Hz
    long c = encCount;
    long dc = c - lastSpdCount;
    if (dt == 0) dt = 1;
    lastSpeedCps = (float)dc * 1000.0f / (float)dt;
    lastSpdCount = c;
    lastSpdMs = now;
    unsigned long secs = dt / 1000UL;
    if (lastSpeedCps == 0.0f) stopSecs += secs; else runSecs += secs;
  }
}

// ---------- JSON ----------
void buildJSON(char* out, size_t n) {
  updateSpeedIfDue();
  unsigned long secs = runSecs + stopSecs; // coherente con snapshots
  char spdbuf[16];
  dtostrf(lastSpeedCps, 0, 2, spdbuf); // evita printf float
  snprintf(out, n, "{\"position\":%ld,\"speed\":%s,\"time\":%lu,\"run\":%lu,\"stop\":%lu}",
           encCount, spdbuf, secs, runSecs, stopSecs);
}

// ---------- CORS / headers ----------
void sendCORS(EthernetClient &c) {
  c.println(F("Access-Control-Allow-Origin: *"));
  c.println(F("Access-Control-Allow-Methods: GET, POST, OPTIONS"));
  c.println(F("Access-Control-Allow-Headers: Content-Type"));
}
void send204(EthernetClient &c) {
  c.println(F("HTTP/1.1 204 No Content"));
  sendCORS(c);
  c.println(F("Content-Length: 0"));
  c.println(F("Connection: close"));
  c.println();
}
void send200Json(EthernetClient &c, const char* json) {
  c.println(F("HTTP/1.1 200 OK"));
  sendCORS(c);
  c.println(F("Content-Type: application/json"));
  c.println(F("Cache-Control: no-cache"));
  c.print  (F("Content-Length: "));
  c.println((int)strlen(json));
  c.println(F("Connection: close"));
  c.println();
  c.print(json);
}
void send200HtmlHead(EthernetClient &c) {
  c.println(F("HTTP/1.1 200 OK"));
  sendCORS(c);
  c.println(F("Content-Type: text/html; charset=utf-8"));
  c.println(F("Connection: close"));
  c.println();
}

// ---------- Snapshots (EEPROM ring) ----------
int snapAddrOf(int slot) { return SNAP_BASE_ADDR + slot * (int)sizeof(Snapshot); }
bool readSnapshot(int slot, Snapshot &s) {
  int addr = snapAddrOf(slot); EEPROM.get(addr, s);
  if (s.magic != SNAP_MAGIC) return false;
  uint8_t calc = crc8((uint8_t*)&s, sizeof(Snapshot)-1);
  return (calc == s.crc);
}
void writeSnapshot(int slot, uint32_t seq, long enc, uint32_t run, uint32_t stop) {
  Snapshot s; s.magic=SNAP_MAGIC; s.seq=seq; s.enc=enc; s.run=run; s.stop=stop;
  s.crc = crc8((uint8_t*)&s, sizeof(Snapshot)-1);
  int addr = snapAddrOf(slot); EEPROM.put(addr, s);
}
bool restoreFromSnapshots() {
  Snapshot best; bool have=false;
  for (int i=0; i<SNAP_SLOTS; ++i) { Snapshot s; if (readSnapshot(i, s)) { if (!have || s.seq>best.seq){ best=s; have=true; } } }
  if (have) {
    encCount = best.enc; lastSpdCount = best.enc;
    runSecs = best.run;  stopSecs = best.stop;
    tStartMs = millis() - (unsigned long)((best.run + best.stop) * 1000UL);
    lastSnapEnc = best.enc; snapSeq = best.seq;
  }
  return have;
}
void maybeSnapshot() {
  unsigned long now = millis();
  if ((now - lastSnapMs) >= (unsigned long)cfg.snapSec*1000UL ||
      labs(encCount - lastSnapEnc) >= (long)cfg.snapDelta) {
    wdt_reset();
    snapSeq++; int slot = snapSeq % SNAP_SLOTS;
    writeSnapshot(slot, snapSeq, encCount, runSecs, stopSecs);
    lastSnapMs = now; lastSnapEnc = encCount;
  }
}

// ---------- Página mínima / y /config ----------
void sendConfigPage(EthernetClient &c) {
  char ip[16], gw[16], sm[16], dns[16], host[16];
  ipToStr(cfg.ip, ip, sizeof(ip)); ipToStr(cfg.gw, gw, sizeof(gw));
  ipToStr(cfg.sm, sm, sizeof(sm)); ipToStr(cfg.dns, dns, sizeof(dns));
  ipToStr(cfg.host, host, sizeof(host));

  send200HtmlHead(c);
  c.print(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Config</title><style>body{font-family:system-ui;margin:12px}input,select{width:100%;margin:4px 0;padding:6px}"
            "label{font-size:12px;opacity:.7;display:block}form{max-width:440px}small{opacity:.7}</style></head><body>"));
  c.print(F("<h3>Encoder Reporter</h3><form method='POST' action='/config'>"));
  c.print(F("<label>DHCP <input type='checkbox' name='dhcp' ")); if(cfg.dhcp) c.print(F("checked")); c.print(F("></label>"));
  c.print(F("<label>IP <input name='ip' value='")); c.print(ip); c.print(F("'></label>"));
  c.print(F("<label>Gateway <input name='gw' value='")); c.print(gw); c.print(F("'></label>"));
  c.print(F("<label>Subnet <input name='sm' value='")); c.print(sm); c.print(F("'></label>"));
  c.print(F("<label>DNS <input name='dns' value='")); c.print(dns); c.print(F("'></label>"));

  c.print(F("<label>Proto <select name='proto'>"));
  c.print(F("<option value='http'")); if(cfg.proto==0) c.print(F(" selected")); c.print(F(">http</option>"));
  c.print(F("<option value='udp'"));  if(cfg.proto==1) c.print(F(" selected")); c.print(F(">udp</option>"));
  c.print(F("</select></label>"));

  c.print(F("<label>Host (IP) <input name='host' value='")); c.print(host); c.print(F("'></label>"));
  c.print(F("<label>Port <input name='port' value='")); c.print(cfg.port); c.print(F("'></label>"));
  c.print(F("<label>Path <input name='path' value='")); c.print(cfg.path); c.print(F("'></label>"));
  c.print(F("<label>Interval (ms) <input name='interval' value='")); c.print(cfg.intervalMs); c.print(F("'></label>"));
  c.print(F("<label>PPM (pulsos/metro) <input name='ppm' value='")); c.print(cfg.ppm); c.print(F("'></label>"));
  c.print(F("<label>Envío periódico <input type='checkbox' name='enabled' ")); if(cfg.enabled) c.print(F("checked")); c.print(F("></label>"));

  c.print(F("<hr><h4>Confiabilidad</h4>"));
  c.print(F("<label>Watchdog <input type='checkbox' name='wdt' ")); if(cfg.wdt_en) c.print(F("checked")); c.print(F("></label>"));
  c.print(F("<label>WDT Timeout (s) <select name='wdt_to'>"));
  int tos[4]={1,2,4,8}; for (int k=0;k<4;k++){ int v=tos[k];
    c.print(F("<option value='")); c.print(v); c.print(F("'")); if (cfg.wdt_to==v) c.print(F(" selected")); c.print(F(">")); c.print(v); c.print(F("</option>"));
  }
  c.print(F("</select></label>"));
  c.print(F("<label>Snapshot cada (s) <input name='snapsec' value='")); c.print(cfg.snapSec); c.print(F("'></label>"));
  c.print(F("<label>Snapshot delta (cuentas) <input name='snapdelta' value='")); c.print(cfg.snapDelta); c.print(F("'></label>"));

  c.print(F("<button type='submit'>Guardar</button></form>"
            "<form action='/send-test'><button>Enviar prueba</button></form>"
            "<p><a href='/data'>/data</a> | <a href='/calib'>/calib</a> | <a href='/reset'>/reset</a></p>"
            "<small>Nota: Cambios de red pueden requerir reinicio manual.</small></body></html>"));
}

// ---------- Página /calib ----------
void sendCalibPage(EthernetClient &c) {
  send200HtmlHead(c);
  c.print(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Calib</title><style>body{font-family:system-ui;margin:12px}input{width:100%;padding:6px;margin:4px 0}</style></head><body>"));
  c.print(F("<h3>Calibración PPM</h3>"
            "<p>Ingrese <b>PPM</b> (pulsos por metro) y guarde.</p>"
            "<form method='POST' action='/calib'><label>PPM<input name='ppm'></label><button type='submit'>Guardar</button></form>"
            "<p>Tip: /reset → mover distancia conocida → ver <a href='/data'>/data</a> (position) → PPM = pulsos/metros.</p>"
            "<p><a href='/'>Volver</a></p></body></html>"));
}

// ---------- Envío HTTP/UDP ----------
void sendJSON_HTTP(const char* json) {
  EthernetClient cl;
  IPAddress dst(cfg.host[0],cfg.host[1],cfg.host[2],cfg.host[3]);
  unsigned long t0 = millis();
  if (cl.connect(dst, cfg.port)) {
    wdt_reset();
    cl.print(F("POST ")); wdt_reset();
    cl.print(cfg.path);   wdt_reset();
    cl.println(F(" HTTP/1.1"));
    cl.print(F("Host: "));
    cl.print((uint8_t)cfg.host[0]); cl.print('.');
    cl.print((uint8_t)cfg.host[1]); cl.print('.');
    cl.print((uint8_t)cfg.host[2]); cl.print('.');
    cl.println((uint8_t)cfg.host[3]);
    cl.println(F("Content-Type: application/json"));
    cl.print  (F("Content-Length: ")); cl.println((int)strlen(json));
    cl.println(F("Connection: close"));
    cl.println();
    wdt_reset();
    cl.print(json);
    delay(10);
    cl.stop();
  } else {
    if (millis()-t0 > 500) wdt_reset();
  }
}
void sendJSON_UDP(const char* json) {
  Udp.beginPacket(IPAddress(cfg.host[0],cfg.host[1],cfg.host[2],cfg.host[3]), cfg.port);
  Udp.write((const uint8_t*)json, strlen(json)); Udp.endPacket();
}
void maybePeriodicSend() {
  if (!cfg.enabled || cfg.intervalMs == 0) return;
  unsigned long now = millis();
  if (now - lastSendMs >= cfg.intervalMs) {
    char json[96]; buildJSON(json, sizeof(json));
    wdt_reset(); if (cfg.proto == 0) sendJSON_HTTP(json); else sendJSON_UDP(json);
    lastSendMs = now;
  }
}

// ---------- Helpers HTTP ----------
int readLine(EthernetClient &c, char* buf, int maxlen) {
  int i=0; while (i<maxlen-1) {
    int chTimeout = 800; unsigned long t = millis();
    while (!c.available() && (millis()-t)<(unsigned long)chTimeout) { wdt_reset(); }
    if (!c.available()) break; char ch = c.read(); buf[i++] = ch; if (ch=='\n') break;
  } buf[i]='\0'; return i;
}
bool getParam(const char* body, const char* key, char* out, int n) {
  int klen = strlen(key); const char* p = strstr(body, key); if (!p) return false; p += klen;
  int i=0; while (*p && *p!='&' && i<n-1){ char ch=*p++; if (ch=='+') ch=' '; out[i++]=ch; } out[i]='\0'; return true;
}
bool startsWith(const char* s, const char* pfx) { while(*pfx){ if(*s++ != *pfx++) return false; } return true; }

// ---------- Reset consistente y atómico ----------
void resetStats() {
  noInterrupts();
  encCount = 0;
  lastSpdCount = 0;
  lastSpeedCps = 0.0f;
  lastSpdMs = millis();
  runSecs = 0; stopSecs = 0;
  interrupts();
  tStartMs = millis();
  lastSendMs = millis();
  snapSeq = 0; lastSnapEnc = 0; lastSnapMs = millis();
  writeSnapshot(0, snapSeq, encCount, runSecs, stopSecs);
}

// ---------- Parse y manejo de requests ----------
void handleRequest(EthernetClient &client) {
  char line[128];
  if (readLine(client, line, sizeof(line)) <= 0) { client.stop(); return; }
  char method[8]="", path[64]="/";
  int i=0; while (line[i] && line[i]!=' ' && i<7){ method[i]=line[i]; i++; } method[i]='\0';
  while (line[i]==' ') i++; int j=0; while (line[i] && line[i]!=' ' && j<63){ path[j++]=line[i++]; } path[j]='\0';

  int contentLen = 0;
  while (true) {
    int n = readLine(client, line, sizeof(line));
    if (n<=2) break;
    if (startsWith(line, "Content-Length:") || startsWith(line, "content-length:")) contentLen = atoi(line + 15);
  }

  if (!strcmp(method,"OPTIONS")) { send204(client); return; }

  char body[256]; body[0]='\0';
  if (!strcmp(method,"POST") && contentLen>0) {
    int toRead = contentLen; int k=0;
    while (toRead>0 && k<(int)sizeof(body)-1) { while (!client.available()) { wdt_reset(); } body[k++]=client.read(); toRead--; }
    body[k]='\0';
  }

  if (!strcmp(method,"GET") && (!strcmp(path,"/") || !strcmp(path,"/config"))) {
    sendConfigPage(client);
  }
  else if (!strcmp(method,"POST") && !strcmp(path,"/config")) {
    char v[48]; bool have;
    have = getParam(body,"dhcp=", v, sizeof(v)) || strstr(body,"dhcp=on"); cfg.dhcp = have ? 1 : 0;
    have = getParam(body,"enabled=", v, sizeof(v)) || strstr(body,"enabled=on"); cfg.enabled = have ? 1 : 0;
    have = getParam(body,"wdt=", v, sizeof(v)) || strstr(body,"wdt=on"); cfg.wdt_en = have ? 1 : 0;

    if (getParam(body,"wdt_to=", v, sizeof(v))) { int x=atoi(v); if(x==1||x==2||x==4||x==8) cfg.wdt_to=(uint8_t)x; }
    if (getParam(body,"snapsec=", v, sizeof(v)))   { int x=atoi(v); if(x>5 && x<3600) cfg.snapSec=(uint16_t)x; }
    if (getParam(body,"snapdelta=", v, sizeof(v))) { long x=atol(v); if(x>=0 && x<2000000000L) cfg.snapDelta=(uint16_t)x; }

    if (getParam(body,"ip=", v, sizeof(v)))  { uint8_t t[4]; if(parseIP(v,t)){ memcpy(cfg.ip,t,4); } }
    if (getParam(body,"gw=", v, sizeof(v)))  { uint8_t t[4]; if(parseIP(v,t)){ memcpy(cfg.gw,t,4); } }
    if (getParam(body,"sm=", v, sizeof(v)))  { uint8_t t[4]; if(parseIP(v,t)){ memcpy(cfg.sm,t,4); } }
    if (getParam(body,"dns=", v, sizeof(v))) { uint8_t t[4]; if(parseIP(v,t)){ memcpy(cfg.dns,t,4); } }
    if (getParam(body,"host=", v, sizeof(v))) { uint8_t t[4]; if(parseIP(v,t)){ memcpy(cfg.host,t,4); } }
    if (getParam(body,"proto=", v, sizeof(v))) { cfg.proto = (!strcmp(v,"udp")) ? 1 : 0; }
    if (getParam(body,"port=", v, sizeof(v))) { cfg.port = (uint16_t)atoi(v); }
    if (getParam(body,"path=", v, sizeof(v))) { safe_strcpy(cfg.path, v, sizeof(cfg.path)); }
    if (getParam(body,"interval=", v, sizeof(v))) { cfg.intervalMs = (uint32_t)atol(v); }
    if (getParam(body,"ppm=", v, sizeof(v))) { uint32_t x = (uint32_t)atol(v); if(x>0) cfg.ppm = x; }

    saveConfig(); send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/data")) {
    char json[96]; buildJSON(json, sizeof(json)); send200Json(client, json);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/reset")) {
    resetStats(); send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/send-test")) {
    char json[96]; buildJSON(json, sizeof(json)); if (cfg.proto == 0) sendJSON_HTTP(json); else sendJSON_UDP(json); send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/calib")) {
    sendCalibPage(client);
  }
  else if (!strcmp(method,"POST") && !strcmp(path,"/calib")) {
    char v[32]; if (getParam(body,"ppm=", v, sizeof(v))) { uint32_t x=(uint32_t)atol(v); if(x>0) { cfg.ppm=x; saveConfig(); } }
    send204(client);
  }
  else {
    sendConfigPage(client);
  }
}

// ---------- Watchdog ----------
void applyWatchdogSetting() {
  MCUSR = 0; wdt_disable();
  if (cfg.wdt_en) {
    switch (cfg.wdt_to) { case 1: wdt_enable(WDTO_1S); break; case 2: wdt_enable(WDTO_2S); break;
                          case 8: wdt_enable(WDTO_8S); break; default: wdt_enable(WDTO_4S); break; }
  }
}

// ---------- Encoder ISR (1×, RISING en A; B define sentido) ----------
void IRAM_ATTR onA() {
  // Leer B por puerto directo para minimizar latencia
  uint8_t b = (*ENC_PORTB_IN & ENC_MASKB) ? 1 : 0;
#if ENCODER_DIR_INVERT
  encCount += (b ? +1 : -1);
#else
  encCount += (b ? -1 : +1);
#endif
}
void IRAM_ATTR onB() { /* no usado en 1× */ }

// ---------- Setup ----------
void setup() {
#if DEBUG
  Serial.begin(115200);
#endif
  loadConfig();
  applyWatchdogSetting();

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  // Mapear B a puerto/máscara (para leerlo en la ISR)
  ENC_PORTB_IN = portInputRegister(digitalPinToPort(PIN_ENC_B));
  ENC_MASKB    = digitalPinToBitMask(PIN_ENC_B);

  // Interrupt solo en A (RISING). B NO se usa con interrupción.
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onA, RISING);

  // Ethernet init
  int ok = 0;
  if (cfg.dhcp) ok = Ethernet.begin((uint8_t*)MAC) ? 1 : 0;
  if (!cfg.dhcp || !ok) {
    IPAddress ip(cfg.ip[0],cfg.ip[1],cfg.ip[2],cfg.ip[3]);
    IPAddress gw(cfg.gw[0],cfg.gw[1],cfg.gw[2],cfg.gw[3]);
    IPAddress sm(cfg.sm[0],cfg.sm[1],cfg.sm[2],cfg.sm[3]);
    IPAddress dns(cfg.dns[0],cfg.dns[1],cfg.dns[2],cfg.dns[3]);
    Ethernet.begin((uint8_t*)MAC, ip, dns, gw, sm);
  }
  delay(200);
  server.begin();
  Udp.begin(LOCAL_UDP_PORT);

  // Restaurar snapshot si existe
  restoreFromSnapshots();

  lastSpdMs = millis();
  lastSendMs = millis();
  lastSnapMs = millis();
  lastSnapEnc = encCount;
}

// ---------- Loop ----------
void loop() {
  wdt_reset();

  // HTTP server
  EthernetClient client = server.available();
  if (client) { handleRequest(client); client.stop(); }

  // Envío periódico
  maybePeriodicSend();

  // Snapshot periódico
  maybeSnapshot();
}
