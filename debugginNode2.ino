#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>

#define MESH_SSID     "evakuasi_mesh"
#define MESH_PASSWORD "evakuasi123"
#define MESH_PORT     5555

// ===== GANTI SESUAI NODE =====
#define NODE_ID   4
#define NODE_NAMA "Ruang C"
// 2 = Koridor
// 3 = Ruang B
// 4 = Ruang C (EXIT)
// =============================

#define PIN_SENSOR 4
#define PIN_LED    2

painlessMesh mesh;

const int TOTAL_NODE = 4;
const int EXIT_LEFT  = 1;
const int EXIT_RIGHT = 4;

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 2000;

bool dangerNodes[TOTAL_NODE + 1] = {false};

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

bool isOnPath(int startNode, int targetNode) {
  if (dangerNodes[targetNode] || dangerNodes[startNode]) return false;
  bool visited[TOTAL_NODE + 1] = {false};
  int  parent [TOTAL_NODE + 1];
  int  queue  [TOTAL_NODE + 1];
  memset(parent, -1, sizeof(parent));
  int front = 0, rear = 0;
  visited[startNode] = true;
  queue[rear++] = startNode;
  bool found = false;
  while (front < rear) {
    int cur = queue[front++];
    if (cur == targetNode) { found = true; break; }
    for (int next = 1; next <= TOTAL_NODE; next++) {
      if (graph[cur][next] && !visited[next] && !dangerNodes[next]) {
        visited[next]  = true;
        parent[next]   = cur;
        queue[rear++]  = next;
      }
    }
  }
  if (!found) return false;
  int node = targetNode;
  while (node != -1) {
    if (node == NODE_ID) return true;
    node = parent[node];
  }
  return false;
}

bool shouldLightLED() {
  if (dangerNodes[NODE_ID]) return false;
  bool anyDanger = false;
  for (int i = 1; i <= TOTAL_NODE; i++) {
    if (dangerNodes[i]) { anyDanger = true; break; }
  }
  if (!anyDanger) return false;
  if (NODE_ID == EXIT_LEFT || NODE_ID == EXIT_RIGHT) return true;
  if (!dangerNodes[EXIT_LEFT]  && isOnPath(NODE_ID, EXIT_LEFT))  return true;
  if (!dangerNodes[EXIT_RIGHT] && isOnPath(NODE_ID, EXIT_RIGHT)) return true;
  return false;
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
  Serial.print("[SEND] "); Serial.println(data);
}

void receivedCallback(uint32_t from, String &msg) {
  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, msg)) return;
  int id    = doc["node_id"] | -1;
  int smoke = doc["smoke"]   | -1;
  if (id >= 1 && id <= TOTAL_NODE && smoke != -1) {
    dangerNodes[id] = (smoke == 1);
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
  Serial.printf("Node ID   : %d\n", NODE_ID);
  Serial.printf("Node Name : %s\n", NODE_NAMA);
  Serial.println("===================================");
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_SSID, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  Serial.println("[READY] Node aktif.");
}

void loop() {
  mesh.update();
  bool smoke = detectSmoke();
  dangerNodes[NODE_ID] = smoke;
  bool ledStatus = shouldLightLED();
  digitalWrite(PIN_LED, ledStatus ? HIGH : LOW);
  if (millis() - lastSend >= SEND_INTERVAL) {
    Serial.printf("Node %d | Smoke: %s | LED: %s\n",
      NODE_ID, smoke ? "BAHAYA" : "AMAN", ledStatus ? "ON" : "OFF");
    kirimStatus(smoke, ledStatus);
    lastSend = millis();
  }
}