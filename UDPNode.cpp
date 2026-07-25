#include "UDPNode.h"

UDPNode::UDPNode(unsigned int port) {
    _localPort = port;
}

// --- NEW: Outgoing Ping Engine ---
void UDPNode::pingTargets() {
    for (auto& target : _pollList) {
        target.missedChecks++;
        
        // If it misses 3 checks, trigger the failure callback
        if (target.missedChecks == 3) {
            if (_onLinkStatusChange) {
                _onLinkStatusChange(target.id, false); 
            }
        }
        
        // Broadcast the 'status ?' probe to port 8888
        _udp.beginPacket(target.ipAddress.c_str(), 8888);
        _udp.print("status ?");
        _udp.endPacket();
    }
}

// --- NEW: File System Logic ---
void UDPNode::loadPollListFromFS() {
    _pollList.clear();
    File file = LittleFS.open("/poll_list.txt", "r");
    if (!file) {
        Serial.println("UDPNode: No poll_list.txt found. Starting fresh.");
        return;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Simple CSV parser: id,ip,name
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);

        if (firstComma > 0 && secondComma > 0) {
            PollTarget target;
            target.id = line.substring(0, firstComma).toInt();
            target.ipAddress = line.substring(firstComma + 1, secondComma);
            target.deviceName = line.substring(secondComma + 1);
            target.missedChecks = 0;
            target.lastStatus = -1;
            
            _pollList.push_back(target);
        }
    }
    file.close();
    Serial.printf("UDPNode: Loaded %d targets from FS\n", _pollList.size());
}

void UDPNode::savePollListToFS() {
    File file = LittleFS.open("/poll_list.txt", "w");
    if (!file) {
        Serial.println("UDPNode Error: Could not open poll_list.txt for writing");
        return;
    }
    for (const auto& target : _pollList) {
        file.printf("%02d,%s,%s\n", target.id, target.ipAddress.c_str(), target.deviceName.c_str());
    }
    file.close();
}

bool UDPNode::addPollTarget(int id, String ip, String name) {
    // Check if ID already exists, update it if so
    for (auto& target : _pollList) {
        if (target.id == id) {
            target.ipAddress = ip;
            target.deviceName = name;
            savePollListToFS();
            return true;
        }
    }
    
    // Otherwise, add a new target to the RAM array
    PollTarget target = {id, ip, name, 0, -1};
    _pollList.push_back(target);
    savePollListToFS(); // Immediately commit to flash
    return true;
}

// --- UPDATED: Initialization ---
void UDPNode::begin() {
    loadPollListFromFS(); // Load saved sensors on boot
    _udp.begin(_localPort);
    Serial.printf("UDPNode initialized on port %d\n", _localPort);
}

// --- UPDATED: Main Loop ---
void UDPNode::loop() {
    // 1. Polling Timer
    if (millis() - _lastPollTime >= _pollInterval) {
        _lastPollTime = millis();
        pingTargets();
    }

    // 2. Incoming Packet Parsing
    int packetSize = _udp.parsePacket();
    if (packetSize) {
        String remoteIP = _udp.remoteIP().toString();
        
        // Watchdog Reset: If we hear ANYTHING from an IP on our list, it is alive.
        for (auto& target : _pollList) {
            if (target.ipAddress == remoteIP) {
                // If it was previously dead, trigger the restored callback
                if (target.missedChecks >= 3) {
                    if (_onLinkStatusChange) {
                        _onLinkStatusChange(target.id, true); 
                    }
                }
                target.missedChecks = 0; // Reset watchdog
                break;
            }
        }

        char packetBuffer[255];
        int len = _udp.read(packetBuffer, 254);
        if (len > 0) {
            packetBuffer[len] = 0; 
        }
        
        String packet = String(packetBuffer);
        packet.trim();
        parsePacket(packet);
    }
}

void UDPNode::parsePacket(String packet) {
    int spaceIndex = packet.indexOf(' ');
    if (spaceIndex == -1) {
        // Special case for single-word commands without a space
        if (packet == "list_poll") {
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            if (_pollList.empty()) {
                _udp.print("Poll list is empty");
            } else {
                for (const auto& t : _pollList) {
                    _udp.print(String(t.id) + "," + t.ipAddress + "," + t.deviceName + "\n");
                }
            }
            _udp.endPacket();
        }
        return; 
    }

    String key = packet.substring(0, spaceIndex);
    String value = packet.substring(spaceIndex + 1);
    key.trim();
    value.trim();

    // 1. Intercept 'msg' command
    if (key == "msg") {
        if (value == "?") {
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            _udp.print("ERR: msg is write-only");
            _udp.endPacket();
            return;
        }
        
        int fullId = value.toInt();
        if (fullId > 0 && _onMessageReceived != nullptr) {
            int sensorId = fullId / 100;
            int messageId = fullId % 100;
            _onMessageReceived(sensorId, messageId); // Triggers handleAudioMessage in main.cpp
            
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            _udp.print("OK: Queued msg " + value);
            _udp.endPacket();
        }
        return; 
    }

    // 2. Intercept 'add_poll' command
    if (key == "add_poll") {
        int valSpaceIdx = value.indexOf(' ');
        if (valSpaceIdx > 0) {
            int id = value.substring(0, valSpaceIdx).toInt();
            String ip = value.substring(valSpaceIdx + 1);
            ip.trim();
            
            addPollTarget(id, ip);
            
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            _udp.print("OK: Added poll target " + String(id));
            _udp.endPacket();
        } else {
            _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
            _udp.print("ERR: Expected format 'add_poll ID IP'");
            _udp.endPacket();
        }
        return;
    }

    // 3. Pass all other commands (volume, bright, etc.) to the main sketch
    if (_onCommandReceived != nullptr) {
        _onCommandReceived(key, value);
    }
}

void UDPNode::onMessage(MsgCallback callback) {
    _onMessageReceived = callback;
}

void UDPNode::onCommand(CommandCallback callback) {
    _onCommandReceived = callback;
}

// --- NEW: Callback Setter ---
void UDPNode::onLinkStatus(LinkCallback callback) {
    _onLinkStatusChange = callback;
}