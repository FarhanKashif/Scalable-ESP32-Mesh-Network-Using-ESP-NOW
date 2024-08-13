#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <map>
#include <time.h>
#include <EEPROM.h>
#include <algorithm>
#include <cstdint>

#define MAX_TRIES 3
#define EEPROM_SIZE 512
#define MAX_NODES 9
#define MAC_SIZE 6

/* PROJECT VARIABLES */
uint8_t baseMac[6]; // Base MAC Address of Sender
char base_mac_str[18]; 
int counter = 1; // Session Counter
char *data;
int incoming_data_count = 0;
bool forward_flag = false;
unsigned long RTT, startTime, endTime, RTO = 3000; // Round Trip Time, Start Time, End Time, Retransmission Time Out (Initial RTO = 3 sec)
uint8_t retry_count = 0; // Retry Count for Retransmission
bool append_flag = true; // Append Flag for Base MAC Address
bool Check_Dest_Flag = false; // Check Destination Flag

std::vector<uint8_t*> connected_nodes; // Vector to store connected nodes
std::map<int, bool> receivedpackets; // Track of PacketID's
std::vector<std::vector<uint8_t>> stored_path; // Store Path Array
std::vector<uint8_t> PathToFollow; // Path to Follow

/* KNOWN MACS */
const uint8_t node1[] = {0xEC, 0x62, 0x60, 0x93, 0xC7, 0xA8}; // MAC Address of 1st ESP32
const uint8_t node2[] = {0x48, 0xE7, 0x29, 0xA3, 0x47, 0x40}; // MAC Address of 2nd ESP32-32u 
const uint8_t node3[] = {0x24, 0xDC, 0xC3, 0xC6, 0xAE, 0xCC}; // MAC Address of 3rd ESP32-32u 

// PMK & LMK Keys
static const char *PMK_KEY = "Connection_ESP32"; // 16-byte PMK
static const char *LMK_KEY = "LMK@ESP32_123456"; // 16-byte LMK


/* PACKET STRUCTURE */
typedef struct message {
  unsigned char text[64]; // 64 bytes of text
  //int value; 
  //float temperature;
  int TTL; // Time to live for packet 
  int identification; // 1-> BROADCAST, 2-> DATA
  bool broadcast_Ack; //  0 -> Default Communication, 1 -> Acknowledgement of Broadcast
  bool Data_Ack; // 0 -> Default Communication, 1 -> Acknowledgement of Data
  uint8_t destination_mac[6]; // MAC Address of Receiver
  uint8_t source_mac[6]; // MAC Address of Sender
  int packetID; // Packet ID
  uint8_t Path_Array[MAX_NODES][MAC_SIZE]; // Path Array
  uint8_t Path_Index; // Index of Path Array
  uint8_t Path_Length;  // Length of Path Array
  bool Path_Exist;  // Check if Path Exists
} message_t;

message_t msg, copy_msg;

enum State {
  WAITING_FOR_ACK,
  READY_TO_SEND,
};

State currentState = READY_TO_SEND; // Initial State of Device

// Queue Structure
typedef struct queue_node {
  message_t data;
  uint8_t mac[6];
  struct queue_node *next;
} queue_node_t;

/* FUNCTION DEFINITIONS */
void FollowPathArray(queue_node_t *temp);
void AddToRoutingAndEEPROM(const uint8_t *mac);
void PrintMACTable();
void SaveDataToEEPROM();
void LoadDataFromEEPROM();
void Forward_Message();
bool Configure_Packet(const char *data, int TTL, int identification, bool broadcast_Ack, bool Data_Ack, const uint8_t *destination_mac, const uint8_t *source_mac, bool path_exist);
void Send_Data(const uint8_t *mac);
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status);
void Check_Existing_Peer(const uint8_t* mac);
void On_Data_Receive(const uint8_t* mac, const uint8_t* data, int len);
void Add_Peer(const uint8_t* mac);
void SwitchToEncryption(const uint8_t *mac);
void broadcast();
void ProcessReceivedData();
void readMAC();
bool AppendBaseMAC(uint8_t index);
void SavePathToEEPROM(uint8_t path_arr[MAX_NODES][MAC_SIZE], uint8_t index);
void ReverseArray(uint8_t index, uint8_t path_arr[MAX_NODES][MAC_SIZE]);
void PrintArray(uint8_t path_arr[MAX_NODES][MAC_SIZE], uint8_t index);
bool CheckDestInPath(const uint8_t *mac);
bool LoadPathFromEEPROM(const uint8_t *mac);
void ResetEEPROMLocations();
void StoreContentsInNode(message_t *msg, queue_node_t *new_node);

queue_node_t *front = NULL;
queue_node_t *rear = NULL;

bool AppendBaseMAC(uint8_t index) {
  
  Serial.printf("Inside Append MAC\n");

  if(index >= MAX_NODES) {
    Serial.println("Max Nodes Reached. Cannot Append MAC Address.");
    return false;
  }

  // Copy Base MAC Address to Path Array
  memcpy(msg.Path_Array[index], baseMac, 6);
  Serial.printf("Appended MAC at index: %d\n", index);
  msg.Path_Index = ++index; // Increment Index of Path Array
  ++msg.Path_Length; // Increment Length of Path Array
  return true;
}
// Send Data Function
void Send_Data(const uint8_t *mac)
{
  bool dataLoaded = LoadPathFromEEPROM(mac); // Load Path Array from EEPROM
  if(dataLoaded) {
    Serial.println("Path Loaded from EEPROM successfully.");

    msg.Path_Exist = true;

    // // Append MAC
    // if(append_flag && !dataLoaded) {
    //   AppendBaseMAC(msg.Path_Index); // Append Base MAC Address to Path Array
    // }

    //  Make a new node and store the contents of the message in the node
    queue_node_t *new_node = (queue_node_t *)malloc(sizeof(queue_node_t));
    if(new_node == NULL) {
      Serial.println("Memory Allocation Failed.");
      return;
    }

    Serial.println("Storing Contents in Node.");
    StoreContentsInNode(&msg, new_node); // Store Contents in Node
    FollowPathArray(new_node); // Follow Path Array

    free(new_node); // Free Memory

    return;
  }

  Serial.println("Path Not Found in EEPROM.");

  if(esp_now_is_peer_exist(mac)) {
    esp_err_t result = esp_now_send(mac, (uint8_t *) &msg, sizeof(msg)); // Send data to 1st ESP32-32u
    if(result == ESP_OK) {
      Serial.println("Sent with Success.");
      currentState = WAITING_FOR_ACK; // Change State to WAITING_FOR_ACK
    } else {
      Serial.println("Error while sending data.");
      Serial.println(esp_err_to_name(result));
    }
  } else {
    Forward_Message();  // Forward packet to connected Peers
  }
}

// Callback when data is sent
void On_Data_Sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Packet Successfully Sent to: ");
  startTime = millis(); // Start Timer
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

// Set Packet Contents
bool Configure_Packet(const char *text, int TTL, int identification, bool broadcast_Ack, bool Data_Ack, const uint8_t *destination_mac, const uint8_t *source_mac, bool path_exist) {
  
  /*if(mac == NULL || data == NULL || destination_mac == NULL || source_mac == NULL) {
    return false;
  }*/

  memset(&msg, 0, sizeof(msg)); // Clear Packet
  strncpy((char *)msg.text, text, sizeof(msg.text) - 1); // Copy Data to Message
  msg.text[sizeof(msg.text) - 1] = '\0'; // Null Terminate
  msg.TTL = TTL; // Set Time to Live
  msg.identification = identification; // Set Identification
  msg.broadcast_Ack = broadcast_Ack; // Set Acknowledgement
  msg.Data_Ack = Data_Ack; // Set Data Acknowledgement
  int random = esp_random(); // Generate Random Number
  msg.packetID = random; // Set Packet ID
  memcpy(msg.destination_mac, destination_mac, 6); // Set Destination MAC Address
  memcpy(msg.source_mac, source_mac, 6); // Set Source MAC Address
  msg.Path_Exist = path_exist; // Set Path Exist Flag
  msg.Path_Length = 0; // Set Path Length

  if(!path_exist && !broadcast_Ack && (identification == 2) && (!Data_Ack) && (!Check_Dest_Flag)) {  
    bool result = AppendBaseMAC(0); // Append Base MAC Address to Path Array
    if(result) {
      Serial.println("Appended MAC Successfully.");
    } else {
      Serial.println("Failed to Append MAC.");
    }
  }

  memcpy(&copy_msg, &msg, sizeof(msg)); // Make a Buffer of Packet

  return true;
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
  Serial.println("Inside On_Data_Receive Function");
  // Copy data to msg structure
  endTime = millis(); // Stop Timer
  memcpy(&msg, data, sizeof(msg)); 

  // Handle Incoming Data 
  queue_node_t *new_node = (queue_node_t *)malloc(sizeof(queue_node_t));
  if(new_node == NULL) {
    Serial.println("Memory Allocation Failed.");
    return;
  }
  
  // Copy message details to new node
  strncpy((char*)new_node->data.text, (char*)data, sizeof(new_node->data.text) - 1);
  new_node->data.text[sizeof(new_node->data.text) - 1] = '\0';
  memcpy(new_node->mac, mac, 6); 
  new_node->data.identification = msg.identification;
  new_node->data.broadcast_Ack = msg.broadcast_Ack; 
  new_node->data.Data_Ack = msg.Data_Ack;
  new_node->data.TTL = msg.TTL;  
  new_node->data.packetID = msg.packetID;
  memcpy(new_node->data.destination_mac, msg.destination_mac, 6); // Set Destination MAC Address
  memcpy(new_node->data.source_mac, msg.source_mac, 6); // Set Source MAC Address
  new_node->data.Path_Index = (uint8_t)msg.Path_Index;  // Set Path Index
  new_node->data.Path_Length = (uint8_t)msg.Path_Length;  // Set Path Length
  new_node->data.Path_Exist = msg.Path_Exist;  // Set Path Exist Flag
  // Store Path Array in new node
  for(int i=0;i<MAX_NODES;i++) {
    memcpy(new_node->data.Path_Array[i], msg.Path_Array[i], MAC_SIZE);
  }
  new_node->next = NULL;

  //Configure_Packet((char*)new_node->data.text, new_node->data.TTL, new_node->data.identification, new_node->data.broadcast_Ack, new_node->data.Data_Ack, new_node->data.destination_mac, new_node->data.source_mac, new_node->data.Path_Exist);

  if(front == NULL && rear == NULL) {
    front = rear = new_node;
  } else {
    rear->next = new_node;
    rear = new_node;
  }
  Serial.print("TTL: ");
  Serial.println(msg.TTL);
  Serial.printf("Destionation MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", msg.destination_mac[0], msg.destination_mac[1], msg.destination_mac[2], msg.destination_mac[3], msg.destination_mac[4], msg.destination_mac[5]);

  // Check if the message is for this node
  if(memcmp(baseMac, msg.destination_mac, 6) == 0) {
    ++incoming_data_count; // Increment Incoming Data Count
  } 
  // Handle Broadcast Messages
  else if(memcmp(msg.destination_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6) == 0) {
    Serial.println("Broadcast Message Received.");
    ++incoming_data_count; // Increment Incoming Data Count
  } 
  // Handle Messages for Other Nodes
  else if(msg.TTL > 0) {
    ++incoming_data_count; // Increment Incoming Data Count
    Serial.println("Forwarding Message.");
    msg.TTL--; // Decrement TTL
    free(new_node); // Free Memory
    forward_flag = true;
  } else {
    Serial.println("TTL Expired. Discarding Packet.");
    memset(&msg, 0, sizeof(msg)); // Clear Message
    free(new_node); // Free Memory
  }
}

// Process Received Data
void ProcessReceivedData() {
  Serial.print("Packet ID: ");
  Serial.println(msg.packetID);
  // Check if Packet is already received
  if(receivedpackets[msg.packetID]) {
    Serial.println("Packet Already Received. Discarding Duplicate Packet.");
    return;
  }

  receivedpackets[msg.packetID] = true; // Mark Packet as Received

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

  // Packet Forwarding
  if(forward_flag) {
    if(!temp->data.Path_Exist) {
      bool result = AppendBaseMAC(temp->data.Path_Index); // Append Base MAC Address to Path Array
      append_flag = false;  // Set Append Flag to False As MAC already Appended
      if(result) {
        Serial.println("Appended MAC Successfully.");
      } else {
        Serial.println("Failed to Append MAC.");
      }
      PrintArray(msg.Path_Array,temp->data.Path_Index);
      Send_Data(temp->data.destination_mac); // Forward Data to Destination
      forward_flag = false;
      append_flag = true; // Reset Append Flag
    } else {
      Serial.println("Path Exists. Forwarding to Next Node.");
      FollowPathArray(temp);
    }
    return;
  }

  RTT = endTime - startTime; // Calculate Round Trip Time
  switch(temp->data.identification) {
    case 2: // DATA is Received 
      Serial.println("*************************************************");
      Serial.println("");
      Serial.print("Session: ");
      Serial.println(counter);
      Serial.println("Session Started");
      Serial.print("Sender MAC Address: ");

      for (int i = 0; i < 6; i++) {
        Serial.print(temp->data.source_mac[i], HEX);
        if (i < 5) {
          Serial.print(":");
        } 
        if( i == 5) {
          Serial.println(""); // print a new line
        }
      }
      Serial.print("Packet ID: ");
      Serial.println(temp->data.packetID);
      Serial.print("RTT: ");
      Serial.println(RTT);
      if((bool *)temp->data.broadcast_Ack) {  // Process Broadcast Acknowledgement
        Serial.print("Broadcast Acknowldgement Received: ");
        Serial.println(temp->data.broadcast_Ack);
        Serial.println((char *) temp->data.text);
        Check_Existing_Peer(temp->mac); // Check if Peer Exists else Add Peer
        SwitchToEncryption(temp->mac); // Switch to encryption mode
        Serial.println("*************************************************");
        memset(&copy_msg, 0, sizeof(copy_msg)); // Clear the Buffer After Successful Broadcast Acknowledgement
      } else if((bool *)temp->data.Data_Ack) {  // Process Data Acknowledgement
        Serial.print("Data Acknowledgement Received: ");
        Serial.println(temp->data.Data_Ack);
        Serial.println((char *) temp->data.text);
        Serial.println("Session Terminated");
        Serial.println("");
        Serial.println("*************************************************");
        memset(&copy_msg, 0, sizeof(copy_msg)); // Clear the Buffer After Successful Data Acknowledgement
        ReverseArray(temp->data.Path_Index, temp->data.Path_Array); // Reverse the Path Array
        PrintArray(temp->data.Path_Array, temp->data.Path_Index);  // Print Reversed Array
        SavePathToEEPROM(temp->data.Path_Array, temp->data.Path_Index); // Save Path Array to EEPROM
        currentState = READY_TO_SEND; // Change State to READY_TO_SEND
      }
      else {
        Serial.print("Data Received: ");
        Serial.println((char*) temp->data.text);
        Serial.println("Session Terminated");
        Serial.println("");
        Serial.println("*************************************************");
        // Handle Sending Acknowledgement here for Data
        AppendBaseMAC(temp->data.Path_Index); // Append Dst Base MAC Address to Path Array
        PrintArray(msg.Path_Array, temp->data.Path_Index);  // Print Path Array
        memset(&temp->data.Path_Array,0,sizeof(temp->data.Path_Array)); // Clear Path Array
        for(int i=0;i<=temp->data.Path_Index;i++) {
          memcpy(temp->data.Path_Array[i], msg.Path_Array[i], MAC_SIZE);  // Updated with Dst MAC to Path Array
        }
        ReverseArray(temp->data.Path_Index, temp->data.Path_Array); // Reverse the Path Array
        PrintArray(temp->data.Path_Array, temp->data.Path_Index);  // Print Reversed Array
        Serial.println("Saving Path to EEPROM");
        SavePathToEEPROM(temp->data.Path_Array, temp->data.Path_Index); // Save Path Array to EEPROM
        //PrintArray(temp->data.Path_Array, temp->data.Path_Index);  // Print Reversed Array
        // Check if Dest MAC already in Path Array. True -> Don't Save in EEPROM False -> Save in EEPROM
        Configure_Packet("Ack from Node 3", 10, 2, false, true, temp->data.source_mac, baseMac, true); // Configure Packet
        msg.Path_Index = temp->data.Path_Index;  // Copy Path Index to Packet
        // Copy Path to Packet
        for(int i=0;i<=msg.Path_Index;i++) {
          Serial.printf("Copying data at index: %d\n", i);
          memcpy(msg.Path_Array[i], temp->data.Path_Array[i], MAC_SIZE);
        } 
        
        // Reset index to 0
        temp->data.Path_Index = 0;  // Reset Path Index
        //Serial.printf("Total Path Index (msg): %d", msg.Path_Index);
        //Serial.printf("Total Path Index (temp): %d", temp->data.Path_Index);
        // Send Data according to Path
        FollowPathArray(temp);
      }
    break;
    case 1: // BROADCAST is Received
      Serial.println("Broadcast Message Received");
      Serial.print("Data Identification Number: ");
      Serial.println(temp->data.identification);
      memset(&msg, 0, sizeof(msg));
      Configure_Packet("Acknowledgement from Node 2", 0, 2, true, false, temp->mac, baseMac, false); // Configure Packet
      msg.Path_Index = 0;  // Set Path Index to 0
      Check_Existing_Peer(temp->mac); // Check if Peer Exists
      Send_Data(temp->mac); // Send Data to Sender
      SwitchToEncryption(temp->mac); // Switch to encryption mode
    break;
    default:  // Unknown Message
      Serial.println("Unknown Message Received");
    break;
  }

  counter++;  // Increment session counter

  free(temp); // Free memory
}

// Add Peer to Routing Table
void Add_Peer(const uint8_t* mac) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  
  for(uint8_t i = 0; i<16; i++) {
    peerInfo.lmk[i] = LMK_KEY[i];
  }

  peerInfo.encrypt = false; // Enable Encryption Mode

  esp_err_t status = esp_now_add_peer(&peerInfo);
  
  // Store the MAC in Vector
  uint8_t *mac_copy = (uint8_t *)malloc(6);
  if(mac_copy) {
    memcpy(mac_copy, mac, 6);
    connected_nodes.push_back(mac_copy); // Add MAC to connected nodes vector
    Serial.println("MAC Address Copied to Vector Successfully.");
  } else {
    Serial.println("Memory Allocation for MAC Copy Failed.");
  }

  // Print MAC Address
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

// Broadcast Message
void broadcast()
{
  const uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcast_address, 6);

  if(!esp_now_is_peer_exist(broadcast_address)) {
    esp_now_add_peer(&peerInfo);
  }
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Prepare Broadcast Data

  Configure_Packet("Broadcast_Msg", 0, 1, false, false, broadcast_address, baseMac, false); // Configure Packet
  // Send Message
  esp_err_t result = esp_now_send(broadcast_address, (uint8_t *) &msg, sizeof(msg));
  if(result == ESP_OK) {
    Serial.println("Broadcast Message Sent Successfully.");
  } else {
    Serial.println("Error while sending broadcast message.");
    Serial.println(esp_err_to_name(result));  
  }
}

// Packet Forwarding Function
void Forward_Message()
{
  if(connected_nodes.empty()) {
    Serial.println("No Connected Nodes to Forward Message.");
    return;
  }

  if(msg.TTL <= 0) {
    Serial.println("TTL Expired. Discarding Packet.");
    return;
  }

  Serial.println("Forwarding Message to Connected Nodes.");
  // Forward Data to Connected Nodes
  for(auto& peer:connected_nodes) {
    if(memcmp(peer, msg.source_mac, 6) != 0) {  // Avoid Retransmitting to Source
      esp_err_t result = esp_now_send(peer, (uint8_t*)&msg, sizeof(msg));
      if (result == ESP_OK) {
        Serial.printf("Message forwarded successfully to peer with MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", peer[0], peer[1], peer[2], peer[3], peer[4], peer[5]);
        currentState = WAITING_FOR_ACK; // Change State to WAITING_FOR_ACK
      } else {
        Serial.println("Error forwarding message.");
        Serial.println(esp_err_to_name(result));
      }
    }
  }
}

void SwitchToEncryption(const uint8_t *mac) {
  if(esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peerInfo = {};

    if(esp_now_get_peer(mac, &peerInfo) == ESP_OK) {
      if(peerInfo.encrypt) {
        Serial.println("Encryption Mode Already Enabled.");
        Serial.printf("Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", peerInfo.peer_addr[0], peerInfo.peer_addr[1], peerInfo.peer_addr[2], peerInfo.peer_addr[3], peerInfo.peer_addr[4], peerInfo.peer_addr[5]);
        return; // Return if Encryption Mode is already enabled
      } else {
        Serial.println("Encryption Mode Not Enabled. Enabling Encryption Mode.");
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = 0;
        
        for(uint8_t i = 0; i<16; i++) {
          peerInfo.lmk[i] = LMK_KEY[i];
        }
        peerInfo.encrypt = true;
        if(esp_now_del_peer(mac) == ESP_OK) {
          Serial.println("Peer Deleted Successfully.");
        } else {
          Serial.println("Failed to Delete Peer.");
        }
        
        if(esp_now_add_peer(&peerInfo) == ESP_OK) {
          Serial.println("Encryption Mode Successfully Enabled.");
        } else {
          Serial.println("Failed to Add Peer With Encryption.");
        }
      }
    } else {
      Serial.println("Failed to Fetch Peer Information.");
    }
  }
}

void SaveDataToEEPROM() {
  int addr = 0;
  int node_count = 0;
  // Save number of nodes on address 0x00. Used for Retrieval Later
  uint8_t num_nodes = connected_nodes.size();
  EEPROM.write(addr, num_nodes);
  addr += sizeof(uint8_t);
  
  // Save each Node
  for(uint8_t *node : connected_nodes) {
    for(int i = 0; i < connected_nodes.size(); i++) {
      EEPROM.write(addr, node[i]);
      addr += sizeof(uint8_t);
    }
    node_count++;
  }

  EEPROM.commit();  // Commit changes to EEPROM
  Serial.printf("Number of Nodes Saved: %d\n", node_count);
  Serial.println("Successfully Saved Data to EEPROM.");
}

void LoadDataFromEEPROM() {
  int addr = 0;
  uint8_t num_nodes = EEPROM.read(addr);  // Retrieve number of nodes
  addr += sizeof(uint8_t);  // Move to next address
  connected_nodes.clear(); // Clear the vector before loading new data

  // Load each node
  for (int i = 0; i < num_nodes; i++) {
    uint8_t* node = (uint8_t*)malloc(MAC_SIZE);
    for (int j = 0; j < MAC_SIZE; j++) {
      node[j] = EEPROM.read(addr);
      addr += sizeof(uint8_t);
    }

    // Check for duplicates before adding
    bool isDuplicate = false;
    for (auto& existingNode : connected_nodes) {
      if (memcmp(existingNode, node, MAC_SIZE) == 0) {
        isDuplicate = true;
        break;
      }
    }

    if (!isDuplicate) {
      //connected_nodes.push_back(node); // Add node to connected nodes vector
      Add_Peer(node);
      SwitchToEncryption(node);
      Serial.println("Node Added to Vector.");
    } else {
      free(node); // Free the allocated memory if it's a duplicate
      Serial.println("Node Already Exists in Vector.");
    }
  }

  Serial.println("Successfully Loaded Data from EEPROM.");
}

void PrintMACTable() {
  int nodes = 0;
  Serial.println("Connected Nodes:");

  for(auto& node:connected_nodes) {
    ++nodes;
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", node[0], node[1], node[2], node[3], node[4], node[5]);
  }

  Serial.printf("Total Nodes: %d", nodes);
}

void AddToRoutingAndEEPROM(const uint8_t *mac) {
  Add_Peer(mac); // Add Peer to ESP-NOW Routing Table
  SwitchToEncryption(mac); // Switch to Encryption Mode
  SaveDataToEEPROM(); // Save Data to EEPROM
}

void PrintMACPath(uint8_t index) {
  Serial.printf("Message Index(+1): %d\n", msg.Path_Index);
  Serial.printf("Path Index in Queue Node: %d\n", index);
  Serial.println("Path Array:");
  for(int i=0;i<=index;i++) {
    Serial.printf("MAC at index %d: %02X:%02X:%02X:%02X:%02X:%02X\n",i, msg.Path_Array[i][0], msg.Path_Array[i][1], msg.Path_Array[i][2], msg.Path_Array[i][3], msg.Path_Array[i][4], msg.Path_Array[i][5]);
  }
}
void ReverseArray(uint8_t index, uint8_t path_arr[MAX_NODES][MAC_SIZE]) {
  uint8_t temp[MAC_SIZE];
  for (int i = 0; i < (index / 2); ++i) {
      memcpy(temp, path_arr[i], MAC_SIZE);  // Copy current to temp
      memcpy(path_arr[i], path_arr[index - i], MAC_SIZE);  // Copy last element to current index
      memcpy(path_arr[index - i], temp, MAC_SIZE);  // Copy current element to last index
  }

  Serial.println("Row reversed Successfully.");
}

void PrintArray(uint8_t path_arr[MAX_NODES][MAC_SIZE], uint8_t index) {
  for(int i=0;i<=index;i++) {
    Serial.printf("MAC at index %d: %02X:%02X:%02X:%02X:%02X:%02X\n",i, path_arr[i][0], path_arr[i][1], path_arr[i][2], path_arr[i][3], path_arr[i][4], path_arr[i][5]);
  }
}

int FindEmptyMemoryLocation() {

  for(int i=0;i<EEPROM_SIZE;i++) {
    if(EEPROM.read(i) == 0xFF) {
      Serial.printf("\nEmpty Memory Location found at %d and %02X\n", i, i);
      return i; // Return address of empty location
    }
  }

  return -1;  // If no empty location found
}

void SavePathToEEPROM(uint8_t path_arr[MAX_NODES][MAC_SIZE], uint8_t index) {
  
  // Serialise the Array into a row vector
  std::vector<uint8_t> Temp_Arr;

  // Store the Path Array in 1D Vector
  for(int i=0;i<=index;i++) {
    for(int j=0;j<MAC_SIZE;j++) {
      Temp_Arr.push_back(path_arr[i][j]);
    }
  }

  Serial.println("Row pushed into Path Array.");

  // Check EEPROM For Destination MAC Address
  bool dest_exists = CheckDestInPath(path_arr[index]);

  /* Don't save if destination Exists */
  if(dest_exists) {
    Serial.println("Destination MAC Already Exists in Path Array. Not Saving to EEPROM.");
    return; // Exit
  }

  Serial.println("Destination MAC does not exist. Finding Empty Memory Location.");
  // Find the next empty memory location in EEPROM and save the stored_path vector there
  int addr = FindEmptyMemoryLocation();
  
  if(addr == -1) {
    Serial.println("Failed to Locate an Empty Memory Location.");
    return; // Exit 
  }
  
  Serial.println("Memory Location Found.");

  /* For First Writing to EEPROM only */
  if(EEPROM.read(511) == 0xFF) {
    EEPROM.write(511, addr);  // Save the address of Path_Array in the last location of EEPROM
  }

  // Save Path Array from Temp_Arr in EEPROM
  for(auto& data:Temp_Arr) {
    EEPROM.write(addr, data);
    addr++;
  }

  EEPROM.write(addr, 0x00); // Store delimeter in EEPROM indicating end of each path
  Serial.printf("Saving Delimeter At Location: %02X", addr);

  EEPROM.commit();  // Commit changes to EEPROM
  Serial.println("Path Array Saved to EEPROM Successfully.");
}

bool CheckDestInPath(const uint8_t *mac) {
  // Load Data from EEPROM in 2D Vector
  int addr = EEPROM.read(511);  // Load the address of Path_Array from last location of EEPROM
  std::vector<std::vector<uint8_t>> Path_Array;
  std::vector<uint8_t> Temp_Arr;

  /* Load MAC From EEPROM into Path_Array */
  for(int i=addr;EEPROM.read(i) != 0xFF;i++) {
    /* Push Array into 2D Vector when path is found */
    if(EEPROM.read(i) == 0x00) {
      Path_Array.push_back(Temp_Arr);
      /* Check if destination MAC exists in Temp_Arr */
      size_t lastMACIndex = Temp_Arr.size() - MAC_SIZE;
      if(memcmp(Temp_Arr.data() + lastMACIndex, mac, MAC_SIZE) == 0) {
        return true;  // Return True if Destination MAC Exists
      }
      Temp_Arr.clear();  // Clear Temp_Arr
    } else {
      Temp_Arr.push_back(EEPROM.read(i));
    }
  }

  return false;
}

void FollowPathArray(queue_node_t *temp) {

    // Copy Path Array to Packet Path Array
    Serial.println("Inside Follow Path Array.");
    PrintArray(msg.Path_Array, MAX_NODES);

    ++temp->data.Path_Index; // Increment Path Index (1)
    msg.Path_Index = temp->data.Path_Index; // Copy Index to Packet Path Index (copies 1 to msg.Path_Index)

    Serial.printf("Sending to MAC: %02X:%02X:%02X:%02X:%02X:%02X at index %d\n", temp->data.Path_Array[temp->data.Path_Index][0], temp->data.Path_Array[temp->data.Path_Index][1], temp->data.Path_Array[temp->data.Path_Index][2], temp->data.Path_Array[temp->data.Path_Index][3], temp->data.Path_Array[temp->data.Path_Index][4], temp->data.Path_Array[temp->data.Path_Index][5], temp->data.Path_Index);

    // Send Packet to Next MAC in Path Array
    esp_err_t result = esp_now_send(temp->data.Path_Array[temp->data.Path_Index], (uint8_t *) &msg, sizeof(msg));
    if(result == ESP_OK) {
      Serial.printf("Acknowledgement Sent with Success to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", temp->data.Path_Array[temp->data.Path_Index][0], temp->data.Path_Array[temp->data.Path_Index][1], temp->data.Path_Array[temp->data.Path_Index][2], temp->data.Path_Array[temp->data.Path_Index][3], temp->data.Path_Array[temp->data.Path_Index][4], temp->data.Path_Array[temp->data.Path_Index][5]);
    } else {
      Serial.println("Error while sending Data to Path.");
      Serial.println(esp_err_to_name(result));
    }

}

void ResetEEPROMLocations() {
  for(int i=0;i<EEPROM_SIZE;i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  delay(100); // Delay to Commit Changes
}

bool LoadPathFromEEPROM(const uint8_t *mac) {

  int addr = EEPROM.read(511);  // Read the address from where Path_Array starts

  // Check if Path Array address exists in EEPROM
  if(addr == 0xFF) {
    Serial.println("No Path Array Found in EEPROM.");
    return false;
  }

  std::vector<uint8_t> Temp_Arr;

  /* Store Path from EEPROM to PathToFollow 1D vector */
  for(int i=addr;EEPROM.read(i) != 0xFF; i++) {
    if(EEPROM.read(i) == 0x00) {
      size_t lastMACIndex = Temp_Arr.size() - MAC_SIZE;
      if(memcmp(Temp_Arr.data() + lastMACIndex, mac, MAC_SIZE) == 0) {
        Serial.println("Destination MAC Found in Path Array.");
        Serial.println("Storing Path in Packet.");

        /* Store Path in Packet */
        for(int i=0;i<Temp_Arr.size();i++) {
          msg.Path_Array[i / MAC_SIZE][i % MAC_SIZE] = Temp_Arr[i];
        }

        Serial.printf("Temp_Arr Size: %d", Temp_Arr.size());
        // Print msg.Path_Array
        for(int i=0;i<MAX_NODES;i++) {
          Serial.printf("MAC at index %d: %02X:%02X:%02X:%02X:%02X:%02X\n",i, msg.Path_Array[i][0], msg.Path_Array[i][1], msg.Path_Array[i][2], msg.Path_Array[i][3], msg.Path_Array[i][4], msg.Path_Array[i][5]);
        }

        return true;  // Return true if Path is found & Stored
      }
      Temp_Arr.clear();
    } else {
      Temp_Arr.push_back(EEPROM.read(i));
    }
  }

  return false;  // Return false if Path is not found
}

void StoreContentsInNode(message_t *msg, queue_node_t *new_node) {
  // Copy message details to new node
  strncpy((char*)new_node->data.text, (char*)msg->text, sizeof(new_node->data.text) - 1);
  memcpy(new_node->mac, msg->source_mac, 6); 
  new_node->data.identification = msg->identification;
  new_node->data.broadcast_Ack = msg->broadcast_Ack; 
  new_node->data.Data_Ack = msg->Data_Ack;
  new_node->data.TTL = msg->TTL;  
  new_node->data.packetID = msg->packetID;
  memcpy(new_node->data.destination_mac, msg->destination_mac, 6); // Set Destination MAC Address
  memcpy(new_node->data.source_mac, msg->source_mac, 6); // Set Source MAC Address
  new_node->data.Path_Index = (uint8_t)msg->Path_Index;  // Set Path Index
  new_node->data.Path_Length = (uint8_t)msg->Path_Length;  // Set Path Length
  new_node->data.Path_Exist = msg->Path_Exist;  // Set Path Exist Flag
  new_node->data.Path_Index = (uint8_t)msg->Path_Index;  // Set Path Index
  // Store Path Array in new node
  for(int i=0;i<MAX_NODES;i++) {
    memcpy(new_node->data.Path_Array[i], msg->Path_Array[i], MAC_SIZE);
  }

  Serial.printf("Printing Path Array\n");
  //Print the array
  for(int i=0;i<MAX_NODES;i++) {
    Serial.printf("MAC at index %d: %02X:%02X:%02X:%02X:%02X:%02X\n",i, new_node->data.Path_Array[i][0], new_node->data.Path_Array[i][1], new_node->data.Path_Array[i][2], new_node->data.Path_Array[i][3], new_node->data.Path_Array[i][4], new_node->data.Path_Array[i][5]);
  }
  
  new_node->next = NULL;
}
void setup() {
  
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE); // Initialize EEPROM
  // Initialize WiFi and Set in Station Mode
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);  
  if(esp_wifi_init(NULL) != ESP_OK) {
    Serial.println("Failed to initialize WiFi");
  }
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start(); 

  readMAC(); //Read MC MAC Addr

  esp_now_init(); // Initialize ESP-NOW

  esp_now_set_pmk((uint8_t *) PMK_KEY); // Set PMK Key

  Add_Peer(node2); // Add 1st ESP32 32u Peer
  SwitchToEncryption(node2);
  //delay(50);
  //Add_Peer(node3); // Add 2nd ESP32 32u Peer  
  //SwitchToEncryption(node3);
  //delay(100);
  //Add_Peer(node1);
  //SwitchToEncryption(node1);
  //delay(100);
  ResetEEPROMLocations();
  // AddToRoutingAndEEPROM(node2);
  // AddToRoutingAndEEPROM(node3);

  // LoadDataFromEEPROM();
  // delay(5);
  // PrintMACTable();

  esp_now_register_send_cb(On_Data_Sent); // Register send_cb function
  esp_now_register_recv_cb(On_Data_Receive); // Register receive_cb function  
  
  //sprintf((char*) msg.text, "Hello from Node 1"); // Prepare data to send
  //Configure_Packet("Hello from Node 1", 3, 2, false, false, rand() % 16777216, node3, node1); // Configure Packet 

  delay(1000);
  srand(time(NULL));
  //esp_now_send(&esp32_32u_2.peer_addr[0], (uint8_t *) data, sizeof(msg)); // Send data to 2nd ESP32 32u
}

int flag = 0;
int prev_time = 0;
int broadcast_prev_time = 0;

void loop() {
  
  while(incoming_data_count > 0) {
    Serial.println(incoming_data_count);
    ProcessReceivedData();
    incoming_data_count--;
  }

  /*if(forward_flag) {
    Forward_Message();
    forward_flag = false;
  }*/

  /*if(millis() - prev_time > 5000) {
    msg.Path_Index = 0;
    Send_Data(node2);
    prev_time = millis();
  }*/

 if(currentState == READY_TO_SEND) {
  Check_Dest_Flag = CheckDestInPath(node3);
  msg.Path_Index = 0;
  Configure_Packet("Hello from Node 1", 3, 2, false, false, node3, node1, false); // Configure Packet
  Send_Data(node3);
  Check_Dest_Flag = false;
  currentState = WAITING_FOR_ACK; // Change State to WAITING_FOR_ACK
 }
 
 if((currentState == WAITING_FOR_ACK) && (millis() - startTime > RTO) && (retry_count < MAX_TRIES)) {
  /* Packet Sent */
  /* Acknowledgement not received within (3, 6, 12) sec */
  /* Retransmit Packet */
  retry_count++;
  RTO *= 2; // Double RTO for Next Attempt (3, 6, 12) sec
  Serial.println("Retransmitting Packet.");
  Configure_Packet((char *)copy_msg.text, copy_msg.TTL, copy_msg.identification, copy_msg.broadcast_Ack, copy_msg.Data_Ack, copy_msg.destination_mac, copy_msg.source_mac, copy_msg.Path_Exist); // Configure Packet
  Send_Data(msg.destination_mac); // Retransmit Packet to Destination
 }

 if(retry_count >= MAX_TRIES) { // Maximum Retransmission Attempts Reached
  Serial.println("Maximum Retransmission Attempts Reached. Terminating Session.");
  Serial.println("Discarding Packet.");
  retry_count = 0; // Reset Retry Count
  RTO = 3000; // Reset RTO
  memset(&copy_msg, 0, sizeof(copy_msg)); // Clear Buffer
  memset(&msg, 0, sizeof(msg)); // Clear Packet Buffer
  currentState = READY_TO_SEND; // Change State to READY_TO_SEND
  while(1); // Halt the Program
 }

  /*if(millis() - prev_time > 5000) {
    Forward_Message();
    prev_time = millis();
  }*/

  // Send Broadcast Msg after 5 seconds
  /*if(millis() - broadcast_prev_time > 10000) {
    broadcast();
    broadcast_prev_time = millis();
  }*/

  /* Check Remaining Stack Size */
  /*UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
  printf("Stack high water mark: %u bytes\n", stackHighWaterMark * sizeof(StackType_t));*/

  delay(50);  // Delay to Process Data
}
