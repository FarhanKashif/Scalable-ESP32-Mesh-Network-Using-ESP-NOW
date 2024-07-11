#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

void Send_Data(const uint8_t *mac);
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status);
void Check_Existing_Peer(const uint8_t* mac);
void On_Data_Receive(const uint8_t* mac, const uint8_t* data, int len);
void Add_Peer(const uint8_t* mac);
void readMAC();

uint8_t baseMac[6]; // Base MAC Address of Sender
char base_mac_str[18]; 
int counter = 1; // Session Counter
char *data;

const uint8_t esp32_1[] = {0x48, 0xE7, 0x29, 0xA3, 0x47, 0x40}; // MAC Address of 1st ESP32-32u 
const uint8_t esp32_2[] = {0x24, 0xDC, 0xC3, 0xC6, 0xAE, 0xCC}; // MAC Address of ESP32 32 2nd

typedef struct message {
  unsigned char text[64]; // 64 bytes of text
  //int value; 
  //float temperature; 
  //bool flag; 
} message_t;

message_t msg;

void Send_Data(const uint8_t *mac)
{
  esp_err_t result = esp_now_send(mac, (uint8_t *) msg.text, sizeof(msg.text)); // Send data to 1st ESP32-32u
  if(result == ESP_OK) {
    Serial.println("Sent with Success.");
  } else {
    Serial.println("Error while sending data.");
    Serial.println(esp_err_to_name(result));
  }

}

// Callback when data is sent
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Packet Successfully Sent to: ");
  for(int i=0;i<5;i++) {
    Serial.print(mac_addr[i], HEX);
    if(i < 5) {
      Serial.print(":");
    }
    if(i==5) {
      Serial.println(""); // Print Next Line
    }
  }
}

// Check if Peer Already Exists
void Check_Existing_Peer(const uint8_t* mac)
{
  bool exists = esp_now_is_peer_exist(mac);
  if(!exists) {
    Serial.println("New Peer Found.");
    Serial.println("Adding Peer");
    Add_Peer(mac);  // Add New Peer to network
  } else {
    Serial.println("No new Peer Found");
  }

}

// Callback when data is received
void On_Data_Receive(const uint8_t* mac, const uint8_t* data, int len) {

  Check_Existing_Peer(mac); // Check if peer already exists

  Serial.println("*************************************************");
  Serial.println("");
  Serial.print("Session: ");
  Serial.println(counter);
  Serial.println("Session Started");
  Serial.print("MAC Address: ");

  for (int i = 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5) {
      Serial.print(":");
    } 
    if( i == 5) {
      Serial.println(""); // print a new line
    }
  }

  Serial.print("Data Received: ");
  Serial.println((char*) data);
  Serial.println("Session Terminated");
  Serial.println("");
  Serial.println("*************************************************");
  counter++;  // Increment session counter
}

void Add_Peer(const uint8_t* mac) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  esp_err_t status = esp_now_add_peer(&peerInfo);
  if (status == ESP_OK) {
    Serial.println("Peer Added Successfully");
    Serial.print("Peer MAC: ");
    for (int i = 0; i < 6; i++) {
      Serial.print(mac[i], HEX);
      if (i < 5) {
        Serial.print(":");
      } 
      if (i == 5) {
        Serial.println(""); // print a new line
      }
    }
  } else {
    Serial.println("Error Adding Peer.");
    Serial.println(esp_err_to_name(status));
  }
}

// Read MAC Address of ESP32
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
  
  // // Set the role of first peer
  // memcpy(esp32_32u.peer_addr, esp32_1, 6);
  // esp32_32u.channel = 0;
  // esp32_32u.encrypt = false;

  // Set the role of second peer
  /*memcpy(esp32_32u_2.peer_addr, esp32_2, 6);
  esp32_32u_2.channel = 0;
  esp32_32u_2.encrypt = false;*/

  // Add Peers
  // if(esp_now_add_peer(&esp32_32u) != ESP_OK) {
  //   Serial.println("Failed to add ESP32 32u");
  // } else {
  //   Serial.println("ESP32 32u Peer added");
  // }

  Add_Peer(esp32_1); // Add 1st ESP32 32u Peer
  Add_Peer(esp32_2); // Add 2nd ESP32 32u Peer  

  /*if(esp_now_add_peer(&esp32_32u_2) != ESP_OK) {
    Serial.println("Failed to add ESP32 32u 2");
  } else {
    Serial.println("ESP32 32u 2 Peer added");
  }*/

  esp_now_register_send_cb(On_Data_Sent); // Register send_cb function
  esp_now_register_recv_cb(On_Data_Receive); // Register receive_cb function  

  // Prepare data to send
  sprintf((char*) msg.text, "Hello from ESP32-32");

  delay(1000);
  //esp_now_send(&esp32_32u_2.peer_addr[0], (uint8_t *) data, sizeof(msg)); // Send data to 2nd ESP32 32u
}


void loop() {

  Send_Data(esp32_1);

  delay(5000);  // Send data after 5 seconds
}
