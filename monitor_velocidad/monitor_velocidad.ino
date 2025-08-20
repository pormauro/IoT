/*  Encoder Reporter UNO (HTTP, UDP) - W5100/W5500
    NOTA: HTTPS/TLS no es posible en ATmega328P por limitaciones de memoria.
    Servidor HTTP con:
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
    Encoder: pines 2 (INT0) y 3 (INT1), cuadratura por interrupciones.

    Optimización tamaño: sin String (salvo internos de la lib), F() en literales,
    JSON en buffer char, HTML ultra-breve, logs desactivables.

    Compila en Arduino UNO + Ethernet (W5100/W5500).
*/

#define DEBUG 0

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>

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

// ---------- Encoder (ISR) ----------
volatile long encCount = 0;
volatile uint8_t lastA = 0, lastB = 0;

// ---------- Tiempo/velocidad ----------
unsigned long tStartMs = 0;
unsigned long lastSpdMs = 0;
long lastSpdCount = 0;
float lastSpeedCps = 0.0f; // cuentas/seg
unsigned long runSecs = 0;
unsigned long stopSecs = 0;

// ---------- Envío periódico ----------
unsigned long lastSendMs = 0;

// ---------- Utilidades pequeñas ----------
static inline void safe_strcpy(char* dst, const char* src, size_t n) {
  if (!n) return;
  size_t i=0; for(; i<n-1 && src && src[i]; ++i) dst[i]=src[i];
  dst[i]='\0';
}
static bool parseIP(const char* s, uint8_t out[4]) {
  if(!s) return false;
  uint16_t acc = 0; uint8_t dots = 0;
  for (const char* p=s; ; ++p) {
    char c = *p;
    if (c >= '0' && c <= '9') {
      acc = acc*10 + (c - '0');
      if (acc > 255) return false;
    } else if (c=='.' || c=='\0') {
      if (dots>3) return false;
      out[dots++] = (uint8_t)acc; acc = 0;
      if (c=='\0') break;
    } else return false;
  }
  return dots==4;
}
static void ipToStr(const uint8_t ip[4], char* buf, size_t n) {
  snprintf(buf, n, "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
}

// ---------- Configuración persistente ----------
struct Config {
  uint32_t magic;      // 0xC0FFEE01
  uint8_t  version;    // 1
  uint8_t  dhcp;       // 0/1
  uint8_t  ip[4];
  uint8_t  gw[4];
  uint8_t  sm[4];
  uint8_t  dns[4];
  uint8_t  enabled;    // envío periódico
  uint8_t  proto;      // 0=http, 1=udp
  uint8_t  host[4];    // IP destino (sin DNS)
  uint16_t port;       // destino
  char     path[32];   // para HTTP POST
  uint32_t intervalMs; // intervalo envío
  uint32_t ppm;        // pulsos por metro
} cfg;

const uint32_t CFG_MAGIC = 0xC0FFEE01;
const int EEPROM_ADDR = 0;

void setDefaults() {
  cfg.magic = CFG_MAGIC;
  cfg.version = 1;
  cfg.dhcp = 1;
  cfg.ip[0]=192; cfg.ip[1]=168; cfg.ip[2]=1; cfg.ip[3]=120;
  cfg.gw[0]=192; cfg.gw[1]=168; cfg.gw[2]=1; cfg.gw[3]=1;
  cfg.sm[0]=255; cfg.sm[1]=255; cfg.sm[2]=255; cfg.sm[3]=0;
  cfg.dns[0]=8; cfg.dns[1]=8; cfg.dns[2]=8; cfg.dns[3]=8;
  cfg.enabled = 0;
  cfg.proto = 0; // http
  cfg.host[0]=192; cfg.host[1]=168; cfg.host[2]=1; cfg.host[3]=10;
  cfg.port = 80;
  safe_strcpy(cfg.path, "/ingest", sizeof(cfg.path));
  cfg.intervalMs = 5000;
  cfg.ppm = 1000; // por defecto 1000 pulsos = 1m
}

void loadConfig() {
  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != CFG_MAGIC || cfg.version != 1) {
    setDefaults();
    EEPROM.put(EEPROM_ADDR, cfg);
  }
}
void saveConfig() {
  EEPROM.put(EEPROM_ADDR, cfg);
}

#if DEBUG
#define DPRINTLN(x) Serial.println(x)
#define DPRINT(x)   Serial.print(x)
#else
#define DPRINTLN(x)
#define DPRINT(x)
#endif

// ---------- Encoder ISRs (cuadratura simple) ----------
void IRAM_ATTR onA() {
  uint8_t a = (uint8_t)digitalRead(PIN_ENC_A);
  uint8_t b = (uint8_t)digitalRead(PIN_ENC_B);
  if (a != lastA) {
    (a == b) ? encCount++ : encCount--;
    lastA = a;
  }
}
void IRAM_ATTR onB() {
  uint8_t a = (uint8_t)digitalRead(PIN_ENC_A);
  uint8_t b = (uint8_t)digitalRead(PIN_ENC_B);
  if (b != lastB) {
    (a != b) ? encCount++ : encCount--;
    lastB = b;
  }
}

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
  unsigned long secs = (millis() - tStartMs) / 1000UL;
  char spdbuf[16];
  dtostrf(lastSpeedCps, 0, 2, spdbuf); // evita printf float
  snprintf(out, n, "{\"position\":%ld,\"speed\":%s,\"time\":%lu,\"run\":%lu,\"stop\":%lu}", encCount, spdbuf, secs, runSecs, stopSecs);
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

// ---------- Página mínima / y /config ----------
void sendConfigPage(EthernetClient &c) {
  char ip[16], gw[16], sm[16], dns[16], host[16];
  ipToStr(cfg.ip, ip, sizeof(ip));
  ipToStr(cfg.gw, gw, sizeof(gw));
  ipToStr(cfg.sm, sm, sizeof(sm));
  ipToStr(cfg.dns, dns, sizeof(dns));
  ipToStr(cfg.host, host, sizeof(host));

  send200HtmlHead(c);
  c.print(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Config</title><style>body{font-family:system-ui;margin:12px}input,select{width:100%;margin:4px 0;padding:6px}"
            "label{font-size:12px;opacity:.7}form{max-width:420px}small{opacity:.7}</style></head><body>"));
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

  c.print(F("<button type='submit'>Guardar</button></form>"
            "<form action='/send-test'><button>Enviar prueba</button></form>"
            "<p><a href='/data'>/data</a> | <a href='/calib'>/calib</a> | <a href='/reset'>/reset</a></p>"
            "<small>Cambios de red requieren reinicio manual.</small></body></html>"));
}

// ---------- Página /calib ----------
void sendCalibPage(EthernetClient &c) {
  send200HtmlHead(c);
  c.print(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Calib</title><style>body{font-family:system-ui;margin:12px}input{width:100%;padding:6px;margin:4px 0}</style></head><body>"));
  c.print(F("<h3>Calibración PPM</h3>"
            "<p>Opción simple: ingrese <b>PPM</b> (pulsos por metro) y guarde.</p>"
            "<form method='POST' action='/calib'><label>PPM<input name='ppm'></label><button type='submit'>Guardar</button></form>"
            "<p>Tip: /reset → mover una distancia conocida → leer <a href='/data'>/data</a> (position) → PPM = pulsos/metros.</p>"
            "<p><a href='/'>Volver</a></p></body></html>"));
}

// ---------- Envío HTTP/UDP ----------
void sendJSON_HTTP(const char* json) {
  EthernetClient cl;
  IPAddress dst(cfg.host[0],cfg.host[1],cfg.host[2],cfg.host[3]);
  if (cl.connect(dst, cfg.port)) {
    cl.print(F("POST "));
    cl.print(cfg.path);
    cl.println(F(" HTTP/1.1"));
    cl.print(F("Host: "));
    cl.print((uint8_t)cfg.host[0]); cl.print('.');
    cl.print((uint8_t)cfg.host[1]); cl.print('.');
    cl.print((uint8_t)cfg.host[2]); cl.print('.');
    cl.println((uint8_t)cfg.host[3]);
    cl.println(F("Content-Type: application/json"));
    cl.print  (F("Content-Length: "));
    cl.println((int)strlen(json));
    cl.println(F("Connection: close"));
    cl.println();
    cl.print(json);
    delay(10);
    cl.stop();
  }
}
void sendJSON_UDP(const char* json) {
  Udp.beginPacket(IPAddress(cfg.host[0],cfg.host[1],cfg.host[2],cfg.host[3]), cfg.port);
  Udp.write((const uint8_t*)json, strlen(json));
  Udp.endPacket();
}
void maybePeriodicSend() {
  if (!cfg.enabled || cfg.intervalMs == 0) return;
  unsigned long now = millis();
  if (now - lastSendMs >= cfg.intervalMs) {
    char json[96];
    buildJSON(json, sizeof(json));
    if (cfg.proto == 0) sendJSON_HTTP(json); else sendJSON_UDP(json);
    lastSendMs = now;
  }
}

// ---------- Helpers HTTP ----------
int readLine(EthernetClient &c, char* buf, int maxlen) {
  int i=0; while (i<maxlen-1) {
    int chTimeout = 800; // ms
    unsigned long t = millis();
    while (!c.available() && (millis()-t)<(unsigned long)chTimeout) { wdt_reset(); }
    if (!c.available()) break;
    char ch = c.read();
    buf[i++] = ch;
    if (ch=='\n') break;
  }
  buf[i]='\0';
  return i;
}
bool getParam(const char* body, const char* key, char* out, int n) {
  // busca "key=" y copia hasta '&' o fin (sin decode %)
  int klen = strlen(key);
  const char* p = strstr(body, key);
  if (!p) return false;
  p += klen;
  int i=0;
  while (*p && *p!='&' && i<n-1) {
    char ch = *p++;
    if (ch=='+') ch=' '; // mínimo
    out[i++]=ch;
  }
  out[i]='\0';
  return true;
}
bool startsWith(const char* s, const char* pfx) {
  while(*pfx) { if(*s++ != *pfx++) return false; }
  return true;
}

// ---------- Parse y manejo de requests ----------
void handleRequest(EthernetClient &client) {
  char line[128];
  if (readLine(client, line, sizeof(line)) <= 0) { client.stop(); return; }
  // Primera línea: "GET /path HTTP/1.1"
  char method[8]="", path[64]="/";
  // método
  int i=0; while (line[i] && line[i]!=' ' && i<7){ method[i]=line[i]; i++; } method[i]='\0';
  // path
  while (line[i]==' ') i++;
  int j=0; while (line[i] && line[i]!=' ' && j<63){ path[j++]=line[i++]; } path[j]='\0';

  int contentLen = 0;
  // Lee cabeceras hasta línea en blanco
  while (true) {
    int n = readLine(client, line, sizeof(line));
    if (n<=2) break; // \r\n
    if (startsWith(line, "Content-Length:") || startsWith(line, "content-length:")) {
      contentLen = atoi(line + 15);
    }
  }

  // CORS preflight
  if (!strcmp(method,"OPTIONS")) { send204(client); return; }

  // POST body si corresponde
  char body[256]; body[0]='\0';
  if (!strcmp(method,"POST") && contentLen>0) {
    int toRead = contentLen;
    int k=0; while (toRead>0 && k< (int)sizeof(body)-1) {
      while (!client.available()) { wdt_reset(); }
      body[k++] = client.read(); toRead--;
    }
    body[k]='\0';
  }

  // Rutas
  if (!strcmp(method,"GET") && (!strcmp(path,"/") || !strcmp(path,"/config"))) {
    sendConfigPage(client);
  }
  else if (!strcmp(method,"POST") && !strcmp(path,"/config")) {
    // Campos esperados
    char v[48];
    // dhcp/enabled (checkbox)
    bool have;
    have = getParam(body,"dhcp=", v, sizeof(v)) || strstr(body,"dhcp=on");
    cfg.dhcp = have ? 1 : 0;

    have = getParam(body,"enabled=", v, sizeof(v)) || strstr(body,"enabled=on");
    cfg.enabled = have ? 1 : 0;

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

    saveConfig();
    send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/data")) {
    char json[96];
    buildJSON(json, sizeof(json));
    send200Json(client, json);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/reset")) {
    encCount = 0;
    lastSpdCount = 0;
    lastSpdMs = 0;
    lastSpeedCps = 0;
    tStartMs = millis();
    runSecs = 0;
    stopSecs = 0;
    send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/send-test")) {
    char json[96];
    buildJSON(json, sizeof(json));
    if (cfg.proto == 0) sendJSON_HTTP(json); else sendJSON_UDP(json);
    send204(client);
  }
  else if (!strcmp(method,"GET") && !strcmp(path,"/calib")) {
    sendCalibPage(client);
  }
  else if (!strcmp(method,"POST") && !strcmp(path,"/calib")) {
    char v[32];
    if (getParam(body,"ppm=", v, sizeof(v))) {
      uint32_t x=(uint32_t)atol(v); if(x>0) { cfg.ppm=x; saveConfig(); }
    }
    send204(client);
  }
  else {
    // Responder config por defecto para simplicidad (ahorra 404 page)
    sendConfigPage(client);
  }
}

// ---------- Setup ----------
void setup() {
#if DEBUG
  Serial.begin(115200);
#endif
  wdt_enable(WDTO_2S);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  lastA = (uint8_t)digitalRead(PIN_ENC_A);
  lastB = (uint8_t)digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), onB, CHANGE);

  loadConfig();

  // Ethernet init (W5100/W5500 en pin 10 CS por defecto)
  int ok = 0;
  if (cfg.dhcp) {
    ok = Ethernet.begin((uint8_t*)MAC) ? 1 : 0;
  }
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

  tStartMs = millis();
  lastSpdMs = 0;
  lastSendMs = millis();
}

// ---------- Loop ----------
void loop() {
  wdt_reset();

  // HTTP server
  EthernetClient client = server.available();
  if (client) {
    handleRequest(client);
    client.stop();
  }

  // Envío periódico
  maybePeriodicSend();
}
