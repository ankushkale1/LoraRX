#include <SPI.h>
#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h> //https://github.com/Links2004/arduinoWebSockets

// Pin definitions
#define ss 15      // D8 on NodeMCU (GPIO15)
#define rst 4      // D2 on NodeMCU (GPIO4)
#define dio0 5     // D1 on NodeMCU (GPIO5)
#define onboardLED 2 // Onboard LED (GPIO2)

// LoRa frequency
#define LORA_FREQUENCY 433E6 // 433 MHz

// Timeout for no packet received (in milliseconds)
const unsigned long PACKET_TIMEOUT = 30000;
unsigned long lastPacketTime = 0;

// Wi-Fi credentials for the hotspot
const char* ssid = "LoRaRx_Hotspot";
const char* password = ""; // No password for open hotspot

// Global variables
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
String latestMessage = "";

void handleRoot();
void blinkLED();
void sendAck();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, HIGH); // Turn off LED initially

  // Initialize Wi-Fi hotspot
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  while (myIP[0] == 0) {  // Check if the IP is assigned
    delay(100);
    myIP = WiFi.softAPIP();
  }
  Serial.print("Hotspot IP: ");
  Serial.println(myIP);

  // Initialize web server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  // Initialize WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");

  // Initialize LoRa
  Serial.println("LoRa Transmitter");
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    webSocket.broadcastTXT("Starting LoRa failed!");
    while (1);
  }

  LoRa.setSpreadingFactor(12);           // Maximum range
  LoRa.setSignalBandwidth(125E3);        // Default bandwidth
  LoRa.setCodingRate4(5);                // Improved robustness
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN); // Max power with PA_BOOST

  Serial.println("LoRa Initialized");
}

void loop() {
  server.handleClient();      // Handle HTTP requests
  webSocket.loop();           // Handle WebSocket connections

  // Handle incoming LoRa packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    latestMessage = "";
    while (LoRa.available()) {
      latestMessage += (char)LoRa.read();
    }
    latestMessage += " RSSI: " + String(LoRa.packetRssi());
    Serial.print("Received packet: ");
    Serial.println(latestMessage);
    // Update last packet received time
    lastPacketTime = millis();

    // Send the latest message to all connected WebSocket clients
    webSocket.broadcastTXT(latestMessage);
    // Send acknowledgment
    sendAck();
  }

  // Check for timeout
  if (millis() - lastPacketTime > PACKET_TIMEOUT) {
    Serial.println("No packet received for more than 30 seconds. Turning on LED.");
    digitalWrite(onboardLED, LOW); // Turn ON LED (active low)
  } else {
    digitalWrite(onboardLED, HIGH); // Turn OFF LED
  }
}


// Function to blink onboard LED for 1 second
void blinkLED() {
  digitalWrite(onboardLED, LOW);  // Turn LED ON
  delay(1000);                    // Wait for 1 second
  digitalWrite(onboardLED, HIGH); // Turn LED OFF
}

// Function to send an acknowledgment
void sendAck() {
  Serial.println("Sending ACK...");
  webSocket.broadcastTXT("Sending ACK...");
  LoRa.beginPacket();
  LoRa.print("ACK");
  LoRa.endPacket();

  blinkLED(); // Blink the onboard LED
}

// Function to serve the main webpage
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>LoRa RX</title>
    <style>
      body {
        background-color: #121212;
        color: #ffffff;
        font-family: Arial, sans-serif;
      }
      #messages {
        background-color: #1e1e1e;
        color: #00ff00;
        padding: 10px;
        margin: 20px auto;
        width: 90%;
        height: 300px;
        overflow-y: auto;
        white-space: pre-wrap;
        border-radius: 5px;
      }
    </style>
    <script>
      var socket = new WebSocket("ws://" + location.hostname + ":81/");
      socket.onmessage = function(event) {
        const messageDiv = document.getElementById("messages");
        console.info("New Message: " + event.data);
        const newMessage = document.createElement("p");
        newMessage.textContent = event.data;
        messageDiv.appendChild(newMessage);
      };

      socket.onerror = function(error) {
        const messageDiv = document.getElementById("messages");
        const newMessage = document.createElement("p");
        newMessage.textContent = error;
        messageDiv.appendChild("Error:" + newMessage);
      };
    </script>
  </head>
  <body>
    <h1>LoRa Receiver Dashboard</h1>
    <div id="messages">Waiting for data...</div>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("Client %d disconnected\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("Client %d connected\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("Received message from client %d: %s\n", num, payload);
      break;
  }
}
