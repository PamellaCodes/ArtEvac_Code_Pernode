#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define MESH_SSID     "evakuasi_mesh"
#define MESH_PASSWORD "evakuasi123"
#define MESH_PORT     5555

#define NODE_ID   1
#define NODE_NAMA "Ruang A"

#define WIFI_SSID  "Pamella's Phone"
#define WIFI_PASS  "pararampam"
#define SERVER_IP  "172.20.10.3"

#define SERVER_URL "http://" SERVER_IP ":3000/api/data"

#define PIN_SENSOR 4
#define PIN_LED    2

painlessMesh mesh;

const int TOTAL_NODE = 4;
const int EXIT_LEFT  = 1;
const int EXIT_RIGHT = 4;

unsigned long lastSend = 0;
unsigned long lastPush = 0;
const unsigned long SEND_INTERVAL = 2000;
const unsigned long PUSH_INTERVAL = 1000;

bool dangerNodes[TOTAL_NODE + 1] = {false};
bool ledNodes   [TOTAL_NODE + 1] = {false};

int graph[TOTAL_NODE + 1][TOTAL_NODE + 1] = {
  {0, 0, 0, 0, 0},
  {0, 0, 1, 0, 0},
  {0, 1, 0, 1, 0},
  {0, 0, 1, 0, 1},
  {0, 0, 0, 1, 0}
};

bool detectSmoke() {
  return digitalRead(PIN_SENSOR) == LOW;
}

bool shouldLightLED() {
  // Kalau node ini sendiri bahaya, mati
  if (dangerNodes[NODE_ID]) return false;

  // LED Node 1 nyala hanya kalau bahaya ada di node 3 atau 4
  // (orang perlu diarahkan ke EXIT kiri / Node 1)
  bool rightSideDanger = dangerNodes[3] || dangerNodes[4];
  return rightSideDanger;
}

void kirimStatus(bool smoke, bool led) {
  StaticJsonDocument<200> doc;
  doc["type"]      = "node_status";
  doc["node_id"]   = NODE_ID;
  doc["node_name"] = NODE_NAMA;
  doc["smoke"]     = smoke ? 1 : 0;
  doc["led"]       = led   ? 1 : 0;
  String data;
  serializeJson(doc, data);
  mesh.sendBroadcast(data);
}

void pushToDashboard() {
  if (WiFi.status() != WL_CONNECTED) return;
  StaticJsonDocument<512> doc;
  JsonArray nodes = doc.createNestedArray("nodes");
  for (int i = 1; i <= TOTAL_NODE; i++) {
    JsonObject n = nodes.createNestedObject();
    n["node_id"] = i;
    n["smoke"]   = dangerNodes[i] ? 1 : 0;
    n["led"]     = ledNodes[i]    ? 1 : 0;
  }
  String payload;
  serializeJson(doc, payload);
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  http.end();
  Serial.printf("[HTTP] Push: %d\n", code);
}

void receivedCallback(uint32_t from, String &msg) {
  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, msg)) return;
  int id    = doc["node_id"] | -1;
  int smoke = doc["smoke"]   | -1;
  int led   = doc["led"]     | -1;
  if (id >= 1 && id <= TOTAL_NODE && smoke != -1) {
    dangerNodes[id] = (smoke == 1);
    if (led != -1) ledNodes[id] = (led == 1);
    Serial.printf("[RECV] Node %d | %s\n", id, smoke ? "BAHAYA" : "AMAN");
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("[MESH] Terhubung: %u\n", nodeId);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  delay(1000);
  Serial.println("===================================");
  Serial.println("  ArtEvac BRIDGE NODE (EXIT KIRI)");
  Serial.println("===================================");
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_SSID, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500); Serial.print("."); retry++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\n[WiFi] Gagal konek.");
  Serial.println("[READY] Bridge node aktif.");
}

void loop() {
  mesh.update();
  bool smoke = detectSmoke();
  dangerNodes[NODE_ID] = smoke;
  bool ledStatus = shouldLightLED();
  ledNodes[NODE_ID] = ledStatus;
  digitalWrite(PIN_LED, ledStatus ? HIGH : LOW);
  if (millis() - lastSend >= SEND_INTERVAL) {
    kirimStatus(smoke, ledStatus);
    Serial.printf("Node %d | Smoke: %s | LED: %s\n",
      NODE_ID, smoke ? "BAHAYA" : "AMAN", ledStatus ? "ON" : "OFF");
    lastSend = millis();
  }
  if (millis() - lastPush >= PUSH_INTERVAL) {
    pushToDashboard();
    lastPush = millis();
  }
}