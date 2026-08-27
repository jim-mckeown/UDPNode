/**
 * @file UDPNode.h
 * @brief Decentralized UDP Parameter Server and Messaging Framework for ESP32
 * 
 * A lightweight, non-blocking C++ class that standardizes network communication,
 * parameter persistence, and critical alert dispatching across decentralized 
 * ESP32 sensor nodes. Designed to interface directly with the Clock Supervisor
 * audio engine.
 * 
 * Core Features:
 *  - Unified 3-level status state machine (0 = Ready, 1 = Info, 2 = Critical)
 *  - Automatic LittleFS parameter registration and persistent flash binding
 *  - Standalone Wi-Fi auto-reconnect engine and ArduinoOTA integration
 *  - Standardized "msg dd01" critical trigger dispatcher
 * 
 * @version 0.1.3
 * @date July 2026
 * @author Jim McKeown
 * @license MIT License
 * 
 * Repository: https://github.com/jim-mckeown/UDPNode
 */

#ifndef UDP_NODE_H
#define UDP_NODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

enum ParamType { PARAM_INT, PARAM_FLOAT, PARAM_STRING };

struct Parameter {
    String command;
    ParamType type;
    void* varPtr;
    String filename; 
    bool persistent; 
    bool readOnly;   
};

struct PollTarget {
    int id;
    String ipAddress;
    String deviceName;
    int missedChecks;
    int lastStatus;
};

typedef void (*MsgCallback)(int sensorId, int messageId);
typedef void (*CommandCallback)(String key, String value);
typedef void (*LinkCallback)(int sensorId, bool isAlive); 

class UDPNode {
public:
    // Struct moved to public scope so main.cpp can inspect target vectors
    struct SupervisorTarget {
        String ipAddress;
        int statusFlag;
    };

private:
    WiFiUDP _udp;
    unsigned int _localPort;
    
    std::vector<Parameter> _params;
    std::vector<PollTarget> _pollList;
    std::vector<SupervisorTarget> _targetIPs;
    
    MsgCallback _onMessageReceived = nullptr;
    CommandCallback _onCommandReceived = nullptr;
    LinkCallback _onLinkStatusChange = nullptr; 

    const char* _ntpServer = "pool.ntp.org";
    long _gmtOffset_sec = 0;
    int _daylightOffset_sec = 0;

    unsigned long _lastPollTime = 0;
    const unsigned long _pollInterval = 10000; 

    void handlePacket(String packetText, IPAddress remoteIP, uint16_t remotePort);
    void reply(IPAddress ip, uint16_t port, String msg);
    
    void saveParam(const Parameter& param);
    void loadParam(const Parameter& param);
    void savePollListToFS();
    void loadPollListFromFS();
    void saveIPList();
    void loadIPList();
    void pingTargets(); 

public:
    UDPNode(unsigned int port = 8888);
    ~UDPNode();

    bool begin(const char* apName = "Supervisor_Node_AP");
    void loop(); 

    void registerParam(const String& command, int* varPtr, bool persistent = true, bool readOnly = false);
    void registerParam(const String& command, float* varPtr, bool persistent = true, bool readOnly = false);
    void registerParam(const String& command, String* varPtr, bool persistent = true, bool readOnly = false);
    void forceSaveParam(const String& command);

    void onMessage(MsgCallback callback);
    void onCommand(CommandCallback callback);
    void onLinkStatus(LinkCallback callback); 

    bool addPollTarget(int id, String ip, String name = "Pending Name");
    bool removePollTarget(int id);
    bool removeTargetIP(String ipAddress);

    void setDistributedStatus(int newStatus);
    int getHighestUnreadStatus();

    void configureTime(long gmtOffset_sec, int daylightOffset_sec, const char* ntpServer = "pool.ntp.org");
    String getFormattedTime();
    void sendAlert(const String& alertMessage);

    // --- Visibility Accessors & Serial Diagnostics ---
    const std::vector<PollTarget>& getPollList() const;
    const std::vector<SupervisorTarget>& getTargetIPs() const;
    void printSummary(Stream& out = Serial);
};

#endif
