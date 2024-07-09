#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

esp_now_peer_info_t esp32_32u;
//esp_now_peer_info_t esp32_32u_2;

uint8_t baseMac[6]; // Base MAC Address of Sender
char base_mac_str[18]; 
char *data;

const uint8_t esp32_1[] = {0x48, 0xE7, 0x29, 0xA3, 0x47, 0x40}; // MAC Address of ESP32 32 1st
//const uint8_t esp32_2[] = {0x24, 0xDC, 0xC3, 0xC6, 0xAE, 0xCC}; // MAC Address of ESP32 32 2nd

typedef struct message {
  unsigned char text[64]; // 64 bytes of text
  //int value; 
  //float temperature; 
  //bool flag; 
} message_t;

message_t msg;

// Callback when data is sent
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Sent Packet Status...");
  //Serial.print(status);
  Serial.println(status == 1 ? "Success" : "Fail");
}

void readMAC()
{
  if(esp_read_mac(baseMac, ESP_MAC_WIFI_STA) == ESP_OK)
  {
    Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  } else {
    Serial.println("Failed to read MAC address..");
  }
}

void setup() {
  
  Serial.begin(115200);

  // Initialize WiFi and Set in Station Mode
  WiFi.mode(WIFI_STA);  
  if(esp_wifi_init(NULL) != ESP_OK) {
    Serial.println("Failed to initialize WiFi");
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start(); 

  //readMAC(); Read MC MAC Addr

  esp_now_init(); // Initialize ESP-NOW
  
  // Set the role of first peer
  memcpy(esp32_32u.peer_addr, esp32_1, 6);
  esp32_32u.channel = 0;
  esp32_32u.encrypt = false;

  // Set the role of second peer
  /*memcpy(esp32_32u_2.peer_addr, esp32_2, 6);
  esp32_32u_2.channel = 0;
  esp32_32u_2.encrypt = false;*/

  // Add Peers
  if(esp_now_add_peer(&esp32_32u) != ESP_OK) {
    Serial.println("Failed to add ESP32 32u");
  } else {
    Serial.println("ESP32 32u Peer added");
  }

  /*if(esp_now_add_peer(&esp32_32u_2) != ESP_OK) {
    Serial.println("Failed to add ESP32 32u 2");
  } else {
    Serial.println("ESP32 32u 2 Peer added");
  }*/

  // Register send_callback function
  esp_now_register_send_cb(On_Data_Sent);

  // Prepare data to send
  sprintf(base_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  sprintf((char*) msg.text, "Hello from %s", base_mac_str);

  //esp_now_send(&esp32_32u_2.peer_addr[0], (uint8_t *) data, sizeof(msg)); // Send data to 2nd ESP32 32u
}


void loop() {
  // Send Data
  esp_err_t result = esp_now_send(esp32_32u.peer_addr, (uint8_t *) msg.text, sizeof(msg.text)); // Send data to 1st ESP32 32u
  if(result == ESP_OK) {
    Serial.println("Sent with Success.");
  } else {
    Serial.println("Error while sending data.");
  }

  delay(5000);  // Send data after 5 seconds
}
