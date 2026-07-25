#ifndef UDP_NODE_H
#define UDP_NODE_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <vector>

struct PollTarget {
    int id;
    String ipAddress;
    String deviceName;
    int missedChecks;
    int lastStatus;
};

// --- NEW: LinkCallback ---
typedef void (*MsgCallback)(int sensorId, int messageId);
typedef void (*CommandCallback)(String key, String value);
typedef void (*LinkCallback)(int sensorId, bool isAlive); 

class UDPNode {
private:
    WiFiUDP _udp;
    unsigned int _localPort;
    
    std::vector<PollTarget> _pollList;
    
    MsgCallback _onMessageReceived = nullptr;
    CommandCallback _onCommandReceived = nullptr;
    LinkCallback _onLinkStatusChange = nullptr; 

    // --- NEW: Timer Variables ---
    unsigned long _lastPollTime = 0;
    const unsigned long _pollInterval = 10000; // 10 seconds

    void parsePacket(String packet);
    void loadPollListFromFS();
    void savePollListToFS();
    void pingTargets(); 

public:
    UDPNode(unsigned int port = 8888);
    
    void begin();
    void loop(); 
    
    void onMessage(MsgCallback callback);
    void onCommand(CommandCallback callback);
    void onLinkStatus(LinkCallback callback); 

    bool addPollTarget(int id, String ip, String name = "Pending Name");
    bool removePollTarget(int id);
};

#endif