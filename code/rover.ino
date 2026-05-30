/*
  =========================================
  ESP8266 (HW-588A) Rover Controller
  - N20 Motors (via L298N or similar driver)
  - SSD1306 OLED Display (I2C)
  - HC-SR04 Ultrasonic Sensor
  - Web Control Panel (Black & White UI)
  =========================================

  WIRING:
  -------
  OLED (SSD1306 128x64 I2C):
    VCC -> 3.3V
    GND -> GND
    SDA -> D2 (GPIO4)
    SCL -> D1 (GPIO5)

  Ultrasonic (HC-SR04):
    VCC -> 5V (or 3.3V with voltage divider on Echo)
    GND -> GND
    TRIG -> D6 (GPIO12)
    ECHO -> D7 (GPIO13)  <-- use 1k/2k voltage divider to protect ESP8266!

  Motor Driver (L298N or TB6612):
    IN1  -> D3 (GPIO0)   Left Motor Forward
    IN2  -> D4 (GPIO2)   Left Motor Backward
    IN3  -> D5 (GPIO14)  Right Motor Forward
    IN4  -> D8 (GPIO15)  Right Motor Backward
    ENA  -> 3.3V or PWM pin (for speed control)
    ENB  -> 3.3V or PWM pin
    VCC  -> 5-12V (motor supply)
    GND  -> Common GND

  Libraries Required (install via Arduino Library Manager):
    - ESP8266WiFi (built-in with ESP8266 board package)
    - Adafruit_SSD1306
    - Adafruit_GFX
    - Wire (built-in)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── WiFi Credentials ───────────────────────────────────────
const char* ssid     = "TUSHAR";
const char* password = "1111111111";

// ─── OLED Config ────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ─── Ultrasonic Pins ────────────────────────────────────────
#define TRIG_PIN  12  // D6
#define ECHO_PIN  13  // D7

// ─── Motor Pins ─────────────────────────────────────────────
#define IN1  0   // D3 - Left Forward
#define IN2  2   // D4 - Left Backward
#define IN3  14  // D5 - Right Forward
#define IN4  15  // D8 - Right Backward

// ─── Web Server ─────────────────────────────────────────────
ESP8266WebServer server(80);

// ─── State Variables ────────────────────────────────────────
String currentCommand = "STOP";
float distanceCm      = 0;
unsigned long lastSensorRead = 0;
const int SENSOR_INTERVAL    = 100; // ms

// ════════════════════════════════════════════════════════════
// MOTOR CONTROL
// ════════════════════════════════════════════════════════════
void motorStop() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}
void motorForward() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void motorBackward() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}
void motorLeft() {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void motorRight() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
}

void executeCommand(String cmd) {
  if      (cmd == "FORWARD")  motorForward();
  else if (cmd == "BACKWARD") motorBackward();
  else if (cmd == "LEFT")     motorLeft();
  else if (cmd == "RIGHT")    motorRight();
  else                        motorStop();
  currentCommand = cmd;
}

// ════════════════════════════════════════════════════════════
// ULTRASONIC SENSOR
// ════════════════════════════════════════════════════════════
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return 400.0; // out of range
  return duration * 0.0343 / 2.0;
}

// ════════════════════════════════════════════════════════════
// OLED DISPLAY
// ════════════════════════════════════════════════════════════
void updateOLED() {
  display.clearDisplay();

  // Title bar
  display.fillRect(0, 0, 128, 12, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(20, 2);
  display.print("ROVER CONTROL");

  // Distance
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("DISTANCE:");

  display.setTextSize(2);
  display.setCursor(0, 26);
  if (distanceCm > 350) {
    display.print("-- cm");
  } else {
    display.print(distanceCm, 1);
    display.print("cm");
  }

  // Warning bar if too close
  if (distanceCm < 20 && distanceCm > 0) {
    display.setTextSize(1);
    display.fillRect(0, 46, 128, 10, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(18, 48);
    display.print("!! TOO CLOSE !!");
    display.setTextColor(WHITE);
  }

  // Current command
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 56);
  display.print("CMD: ");
  display.print(currentCommand);

  // IP address (small, top right area)
  display.setCursor(70, 16);
  display.setTextSize(1);
  String ip = WiFi.localIP().toString();
  if (ip.length() > 13) ip = ip.substring(ip.lastIndexOf('.'));
  display.print(ip);

  display.display();
}

// ════════════════════════════════════════════════════════════
// WEB SERVER HTML
// ════════════════════════════════════════════════════════════
String buildHTML() {
  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>ROVER</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Bebas+Neue&display=swap');

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  :root {
    --bg: #0a0a0a;
    --fg: #f0f0f0;
    --accent: #ffffff;
    --dim: #444;
    --danger: #ff4444;
    --border: 1px solid #333;
  }

  body {
    background: var(--bg);
    color: var(--fg);
    font-family: 'Share Tech Mono', monospace;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 20px;
    overflow-x: hidden;
  }

  /* Scanline overlay */
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background: repeating-linear-gradient(
      0deg,
      transparent,
      transparent 2px,
      rgba(0,0,0,0.08) 2px,
      rgba(0,0,0,0.08) 4px
    );
    pointer-events: none;
    z-index: 999;
  }

  header {
    width: 100%;
    max-width: 480px;
    text-align: center;
    margin-bottom: 24px;
    border-bottom: 1px solid #222;
    padding-bottom: 12px;
  }

  header h1 {
    font-family: 'Bebas Neue', sans-serif;
    font-size: 3.5rem;
    letter-spacing: 0.3em;
    color: #fff;
    text-shadow: 0 0 30px rgba(255,255,255,0.15);
  }

  header p {
    font-size: 0.65rem;
    color: var(--dim);
    letter-spacing: 0.25em;
    text-transform: uppercase;
    margin-top: 2px;
  }

  /* Status Panel */
  .status-panel {
    width: 100%;
    max-width: 480px;
    background: #111;
    border: var(--border);
    padding: 16px 20px;
    margin-bottom: 20px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
  }

  .stat-box {
    border: var(--border);
    padding: 12px;
    text-align: center;
  }

  .stat-label {
    font-size: 0.6rem;
    letter-spacing: 0.2em;
    color: var(--dim);
    text-transform: uppercase;
    margin-bottom: 6px;
  }

  .stat-value {
    font-family: 'Bebas Neue', sans-serif;
    font-size: 2rem;
    color: #fff;
    transition: color 0.3s;
  }

  .stat-value.danger { color: var(--danger); }
  .stat-value.unit { font-size: 0.9rem; color: var(--dim); margin-left: 2px; }

  /* Distance Bar */
  .dist-bar-wrap {
    width: 100%;
    max-width: 480px;
    margin-bottom: 20px;
  }

  .dist-bar-label {
    font-size: 0.6rem;
    letter-spacing: 0.2em;
    color: var(--dim);
    margin-bottom: 6px;
  }

  .dist-bar {
    width: 100%;
    height: 6px;
    background: #1a1a1a;
    border: var(--border);
    overflow: hidden;
  }

  .dist-bar-fill {
    height: 100%;
    background: #fff;
    transition: width 0.3s ease, background 0.3s;
    width: 0%;
  }

  /* D-Pad Controller */
  .controller {
    width: 100%;
    max-width: 480px;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 8px;
    margin-bottom: 20px;
  }

  .btn {
    aspect-ratio: 1;
    background: #111;
    border: 1px solid #2a2a2a;
    color: #fff;
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.65rem;
    letter-spacing: 0.15em;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 6px;
    user-select: none;
    transition: background 0.1s, border-color 0.1s, transform 0.08s;
    -webkit-tap-highlight-color: transparent;
  }

  .btn svg { width: 22px; height: 22px; }

  .btn:active, .btn.active {
    background: #fff;
    color: #000;
    border-color: #fff;
    transform: scale(0.96);
  }

  .btn:active svg path, .btn.active svg path { stroke: #000; }

  .btn.stop-btn {
    grid-column: 2;
    background: #0d0d0d;
    border-color: #1a1a1a;
  }

  .btn.stop-btn:active, .btn.stop-btn.active {
    background: var(--danger);
    border-color: var(--danger);
    color: #fff;
  }

  .spacer { opacity: 0; pointer-events: none; }

  /* Command Log */
  .log {
    width: 100%;
    max-width: 480px;
    border: var(--border);
    background: #0d0d0d;
    padding: 10px 14px;
    font-size: 0.7rem;
    color: var(--dim);
    letter-spacing: 0.1em;
  }

  .log span { color: #fff; }

  /* Footer */
  footer {
    margin-top: 24px;
    font-size: 0.55rem;
    color: #222;
    letter-spacing: 0.2em;
    text-align: center;
  }
</style>
</head>
<body>

<header>
  <h1>ROVER</h1>
  <p>ESP8266 // HW-588A // CONTROL SYSTEM</p>
</header>

<div class="status-panel">
  <div class="stat-box">
    <div class="stat-label">Distance</div>
    <div class="stat-value" id="dist-val">--<span class="unit">cm</span></div>
  </div>
  <div class="stat-box">
    <div class="stat-label">Command</div>
    <div class="stat-value" id="cmd-val" style="font-size:1.1rem; padding-top:8px;">STOP</div>
  </div>
</div>

<div class="dist-bar-wrap">
  <div class="dist-bar-label">PROXIMITY — 0 cm &nbsp;|&nbsp; 400 cm</div>
  <div class="dist-bar"><div class="dist-bar-fill" id="dist-bar"></div></div>
</div>

<!-- D-Pad -->
<div class="controller">
  <div class="spacer btn"></div>
  <button class="btn" id="btn-F" onpointerdown="send('FORWARD')" onpointerup="send('STOP')" ontouchstart="send('FORWARD')" ontouchend="send('STOP')">
    <svg viewBox="0 0 24 24" fill="none"><path d="M12 19V5M5 12l7-7 7 7" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    FWD
  </button>
  <div class="spacer btn"></div>

  <button class="btn" id="btn-L" onpointerdown="send('LEFT')" onpointerup="send('STOP')" ontouchstart="send('LEFT')" ontouchend="send('STOP')">
    <svg viewBox="0 0 24 24" fill="none"><path d="M19 12H5M12 19l-7-7 7-7" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    LEFT
  </button>
  <button class="btn stop-btn" id="btn-S" onclick="send('STOP')">
    <svg viewBox="0 0 24 24" fill="none"><rect x="5" y="5" width="14" height="14" stroke="#fff" stroke-width="2"/></svg>
    STOP
  </button>
  <button class="btn" id="btn-R" onpointerdown="send('RIGHT')" onpointerup="send('STOP')" ontouchstart="send('RIGHT')" ontouchend="send('STOP')">
    <svg viewBox="0 0 24 24" fill="none"><path d="M5 12h14M12 5l7 7-7 7" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    RIGHT
  </button>

  <div class="spacer btn"></div>
  <button class="btn" id="btn-B" onpointerdown="send('BACKWARD')" onpointerup="send('STOP')" ontouchstart="send('BACKWARD')" ontouchend="send('STOP')">
    <svg viewBox="0 0 24 24" fill="none"><path d="M12 5v14M5 12l7 7 7-7" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    REV
  </button>
  <div class="spacer btn"></div>
</div>

<div class="log">SYS &gt; <span id="log-txt">Waiting for input...</span></div>

<footer>ESP8266 ROVER &mdash; WEB INTERFACE &mdash; ALL RIGHTS RESERVED</footer>

<script>
  const btnMap = { FORWARD:'btn-F', BACKWARD:'btn-B', LEFT:'btn-L', RIGHT:'btn-R', STOP:'btn-S' };
  let activeBtn = null;
  let pollTimer = null;

  function send(cmd) {
    // Visual feedback
    if (activeBtn) document.getElementById(activeBtn)?.classList.remove('active');
    const id = btnMap[cmd];
    if (id) { document.getElementById(id).classList.add('active'); activeBtn = id; }
    document.getElementById('cmd-val').textContent = cmd;
    document.getElementById('log-txt').textContent = 'Sending: ' + cmd + '...';

    fetch('/cmd?action=' + cmd)
      .then(r => r.text())
      .then(t => { document.getElementById('log-txt').textContent = 'ACK: ' + cmd; })
      .catch(() => { document.getElementById('log-txt').textContent = 'ERR: Connection lost'; });
  }

  function pollSensor() {
    fetch('/sensor')
      .then(r => r.json())
      .then(d => {
        const dist = parseFloat(d.distance);
        const el = document.getElementById('dist-val');
        el.innerHTML = (dist > 350 ? '--' : dist.toFixed(1)) + '<span class="unit">cm</span>';
        el.classList.toggle('danger', dist < 20 && dist > 0);

        // Bar: 0cm = full, 400cm = empty (proximity bar)
        const pct = dist > 350 ? 0 : Math.max(0, Math.min(100, (1 - dist/400) * 100));
        const bar = document.getElementById('dist-bar');
        bar.style.width = pct + '%';
        bar.style.background = dist < 20 ? '#ff4444' : dist < 50 ? '#aaa' : '#fff';
      })
      .catch(() => {});
  }

  // Poll sensor every 300ms
  setInterval(pollSensor, 300);
  pollSensor();
</script>
</body>
</html>
)rawhtml";
  return html;
}

// ════════════════════════════════════════════════════════════
// SERVER HANDLERS
// ════════════════════════════════════════════════════════════
void handleRoot() {
  server.send(200, "text/html", buildHTML());
}

void handleCommand() {
  if (server.hasArg("action")) {
    String action = server.arg("action");
    action.toUpperCase();
    executeCommand(action);
    server.send(200, "text/plain", "OK:" + action);
  } else {
    server.send(400, "text/plain", "Missing action param");
  }
}

void handleSensor() {
  String json = "{\"distance\":" + String(distanceCm, 1) + 
                ",\"command\":\"" + currentCommand + "\"}";
  server.send(200, "application/json", json);
}

// ════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // Motor pins
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  motorStop();

  // Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // OLED init
  Wire.begin(4, 5); // SDA=D2(GPIO4), SCL=D1(GPIO5)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 OLED not found!");
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(10, 25);
  display.print("Connecting WiFi...");
  display.display();

  // WiFi
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // Show IP on OLED
  display.clearDisplay();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    display.fillRect(0, 0, 128, 12, WHITE);
    display.setTextColor(BLACK);
    display.setCursor(5, 2);
    display.print("WiFi Connected!");
    display.setTextColor(WHITE);
    display.setCursor(0, 20);
    display.print("IP:");
    display.setCursor(0, 30);
    display.print(WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED - starting AP");
    WiFi.softAP("ROVER_AP", "rover1234");
    display.setCursor(0, 16);
    display.print("AP: ROVER_AP");
    display.setCursor(0, 28);
    display.print("PW: rover1234");
    display.setCursor(0, 40);
    display.print("IP: 192.168.4.1");
  }
  display.display();
  delay(2000);

  // Web server routes
  server.on("/",       handleRoot);
  server.on("/cmd",    handleCommand);
  server.on("/sensor", handleSensor);
  server.begin();
  Serial.println("HTTP server started");
}

// ════════════════════════════════════════════════════════════
// LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();

  // Read sensor every 100ms (non-blocking)
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    distanceCm = readDistance();
    lastSensorRead = millis();

    // Auto-stop if obstacle too close (< 10cm)
    if (distanceCm < 10.0 && distanceCm > 0 && currentCommand == "FORWARD") {
      executeCommand("STOP");
      Serial.println("AUTO-STOP: obstacle detected!");
    }

    updateOLED();
    Serial.print("Dist: "); Serial.print(distanceCm); Serial.println(" cm");
  }
}
