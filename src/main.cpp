#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void Send_Data(const uint8_t *mac);
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status);
void Check_Existing_Peer(const uint8_t* mac);
void On_Data_Receive(const uint8_t* mac, const uint8_t* data, int len);
void Add_Peer(const uint8_t* mac);
void ProcessReceivedData();
void readMAC();

uint8_t baseMac[6]; // Base MAC Address of Sender
char base_mac_str[18]; 
int counter = 1; // Session Counter
char *data;
int incoming_data_count = 0;

const uint8_t node1[] = {0xEC, 0x62, 0x60, 0x93, 0xC7, 0xA8}; // MAC Address of 1st ESP32
//const uint8_t node2[] = {0x48, 0xE7, 0x29, 0xA3, 0x47, 0x40}; // MAC Address of 2nd ESP32-32u 
const uint8_t node3[] = {0x24, 0xDC, 0xC3, 0xC6, 0xAE, 0xCC}; // MAC Address of 3rd ESP32-32u 

// PMK & LMK Keys
static const char *PMK_KEY = "Connection@ESP32"; // 16-byte PMK
static const char *LMK_KEY = "LMK@ESP32_123456"; // 16-byte LMK

// Packet Structure
typedef struct message {
  unsigned char text[64]; // 64 bytes of text
  //int value; 
  //float temperature; 
  //bool flag; 
} message_t;

message_t msg;

// Queue Structure
typedef struct queue_node {
  message_t data;
  uint8_t mac[6];
  struct queue_node *next;
} queue_node_t;

queue_node_t *front = NULL;
queue_node_t *rear = NULL;

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
  }

}

// Callback when data is received
void On_Data_Receive(const uint8_t* mac, const uint8_t* data, int len) {

  ++incoming_data_count; // Increment Incoming Data Count

  // Handle Incoming Data 
  queue_node_t *new_node = (queue_node_t *)malloc(sizeof(queue_node_t));

  if(new_node == NULL) {
    Serial.println("Memory Allocation Failed.");
    return;
  }

  strncpy((char*)new_node->data.text, (char*)data, sizeof(new_node->data.text) - 1); // Copy data to new node
  new_node->data.text[sizeof(new_node->data.text) - 1] = '\0'; // Ensure null termination
  memcpy(new_node->mac, mac, 6);  
  new_node -> next = NULL;

  if(front == NULL && rear == NULL) {
    front = rear = new_node;
  } else {
    rear -> next = new_node;
    rear = new_node;
  }

  // Serial.println("Data Received Successfully.");
  // Serial.println((char *) new_node ->data.text);

}

void ProcessReceivedData() {

  if(front == NULL) {
    Serial.println("Queue is empty. No Data to Process");
    return;
  }

  queue_node_t *temp = front;

  Serial.println("Inside Processing Function");

  front = front->next;  // Point to next node in queue

  if(front == NULL) {
    rear = NULL;
  }


  Serial.println("*************************************************");
  Serial.println("");
  Serial.print("Session: ");
  Serial.println(counter);
  Serial.println("Session Started");
  Serial.print("MAC Address: ");

  for (int i = 0; i < 6; i++) {
    Serial.print(temp->mac[i], HEX);
    if (i < 5) {
      Serial.print(":");
    } 
    if( i == 5) {
      Serial.println(""); // print a new line
    }
  }

  Serial.print("Data Received: ");
  Serial.println((char*) temp->data.text);
  Serial.println("Session Terminated");
  Serial.println("");
  Serial.println("*************************************************");
  counter++;  // Increment session counter

  Check_Existing_Peer(temp->mac); // Check if Peer Exists

  free(temp); // Free memory
}

void Add_Peer(const uint8_t* mac) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;  
  for(uint8_t i = 0; i<16; i++) {
    peerInfo.lmk[i] = LMK_KEY[i];
  }
  peerInfo.encrypt = true;

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
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);  
  if(esp_wifi_init(NULL) != ESP_OK) {
    Serial.println("Failed to initialize WiFi");
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start(); 

  //readMAC(); Read MC MAC Addr

  esp_now_init(); // Initialize ESP-NOW
  
  esp_now_set_pmk((uint8_t *) PMK_KEY); // Set PMK Key
  
  Add_Peer(node1); // Add 1st ESP32 32u Peer
  Add_Peer(node3); // Add 2nd ESP32 32u Peer  

  esp_now_register_send_cb(On_Data_Sent); // Register send_cb function
  esp_now_register_recv_cb(On_Data_Receive); // Register receive_cb function  

  sprintf((char*) msg.text, "Hello from Node 3"); // Prepare data to send

  delay(1000);
  //esp_now_send(&esp32_32u_2.peer_addr[0], (uint8_t *) data, sizeof(msg)); // Send data to 2nd ESP32 32u
}

int flag = 0;
int prev_time = 0;

void loop() {

  while(incoming_data_count > 0) {
    Serial.println(incoming_data_count);
    ProcessReceivedData();
    incoming_data_count--;
  }

  if(millis() - prev_time > 2000) {
    Send_Data(node1);
    prev_time = millis();
  }
  
  /* Check Remaining Stack Size */
  /*UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
  printf("Stack high water mark: %u bytes\n", stackHighWaterMark * sizeof(StackType_t));*/
}
