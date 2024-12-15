// #include <SPI.h>
// #include <LoRa.h>

// // Pin definitions
// #define ss 15      // D8 on NodeMCU (GPIO15)
// #define rst 4      // D2 on NodeMCU (GPIO4)
// #define dio0 5     // D1 on NodeMCU (GPIO5)

// // Frequency (adjust if using a different frequency band)
// #define LORA_FREQUENCY 433E6 // 433 MHz (Europe/Asia)

// void setup() {
//   // Initialize serial communication
//   Serial.begin(115200);
//   while (!Serial);

//   Serial.println("LoRa Receiver");

//   // Configure LoRa module pins
//   LoRa.setPins(ss, rst, dio0);

//   // Initialize LoRa at the specified frequency
//   if (!LoRa.begin(LORA_FREQUENCY)) {
//     Serial.println("Starting LoRa failed!");
//     while (1); // Halt the program if initialization fails
//   }

//   Serial.println("LoRa Initialized");
// }

// void loop() {
//   // Check if a packet is available
//   int packetSize = LoRa.parsePacket();
//   if (packetSize) {
//     Serial.print("Received packet: ");

//     // Read the incoming packet
//     while (LoRa.available()) {
//       Serial.print((char)LoRa.read());
//     }

//     // Display RSSI (Signal Strength)
//     Serial.print(" with RSSI ");
//     Serial.println(LoRa.packetRssi());
//   }
// }

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
  // Serial communication
  Serial.begin(115200);
  while (!Serial);

  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, HIGH); // Turn off LED initially

  // Initialize LoRa
  Serial.println("LoRa Receiver");
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  Serial.println("LoRa Initialized");
  lastPacketTime = millis(); // Initialize last packet time

  // Configure LoRa parameters for maximum range
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);      // Max TX power: 20 dBm
  LoRa.setSpreadingFactor(12); // Spreading Factor: 12 (max range, slower data rate)
  LoRa.setSignalBandwidth(125E3); // Bandwidth: 125 kHz (low bandwidth, higher range)
  LoRa.setCodingRate4(5);   // Coding Rate: 4/5 (robust against interference)

  // Set up Wi-Fi hotspot
  WiFi.softAP(ssid, password);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Wi-Fi hotspot started. IP: ");
  Serial.println(ip);

  // Set up the web server
  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTP server started");

  // Set up WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");
}

void loop() {
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

  // Handle web server and WebSocket
  server.handleClient();
  webSocket.loop();
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
      /* Dark mode styling */
      body {
        background-color: #121212;
        color: #ffffff;
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 0;
      }

      h1 {
        text-align: center;
        color: #00ffcc;
      }

      /* Scrollable box for messages */
      #message-box {
        border: 1px solid #333;
        background-color: #1e1e1e;
        color: #ffffff;
        padding: 10px;
        width: 80%;
        height: 300px;
        margin: 20px auto;
        overflow-y: auto;
        border-radius: 8px;
      }

      /* Latest message styling */
      #latest-message {
        text-align: center;
        font-weight: bold;
        color: #00ffcc;
      }
    </style>
    <script>
      var socket = new WebSocket("ws://" + location.hostname + ":81/");
      
      socket.onmessage = function(event) {
        var messageBox = document.getElementById("message-box");
        
        // Append the new message
        messageBox.innerHTML += event.data + "\n";
        
        // Scroll to the bottom to show the latest message
        messageBox.scrollTop = messageBox.scrollHeight;
        
        // Update latest message
        document.getElementById("latest-message").innerText = event.data;
      };
    </script>
  </head>
  <body>
    <h1>LoRa Receiver</h1>
    
    <div id="latest-message">Waiting for data...</div>
    
    <!-- Scrollable box for message history -->
    <div id="message-box">
      <!-- Messages will be appended here -->
    </div>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    Serial.printf("WebSocket Message Received: %s\n", payload);
  }
}
