### I. Supervisor UDP Command API (Port 8888)

This is the central command dictionary that your remote sensors (and your Termux/Python scripts) can use to interact with the clock. Because everything is unified on port **8888**, any incoming traffic from a sensor will simultaneously reset its watchdog timeout counter.

**Alert & Audio Triggers**

* **`msg <ID>`**: The primary event trigger. It commands the supervisor to queue and play specific audio files. The ID is formatted as `[SensorID][MessageID]`.
* *Example:* `msg 0101` (Triggers Sensor 1, Message 1).
* *Note:* Sending `msg ?` will return an error, as this command is write-only.



**Dynamic Parameter Control**
These commands interact with the variables registered in your `main.cpp` via the `registerParam` function.

* **`<param> <value>`**: Sets a parameter to a new value and saves it to LittleFS if it is flagged as persistent.
* *Example:* `volume 15` or `bright 10`.


* **`<param> ?`**: Queries the supervisor for the current value of a parameter.
* *Example:* `volume ?` (Returns: `OK volume 15`).



**Watchdog & Polling Management**

* **`add_poll <ID> <IP>`**: Commands the supervisor to add a new sensor to its active watchdog polling list. If the ID already exists, it updates the IP address.
* *Example:* `add_poll 01 192.168.1.138`.


* **`list_poll`**: Returns a CSV-formatted list of all sensors currently being tracked by the supervisor's watchdog engine.

**Network & Administration**

* **`new_ssid <SSID>`**: Stages a new WiFi network name in memory.
* **`new_pass <PASS>`**: Stages a new WiFi password in memory.
* **`commit_wifi`**: Applies the staged credentials and reboots the ESP32 to join the new network. If sent without staging credentials first, it wipes the network config and boots the Captive Portal.
* **`forget_wifi`**: Erases all WiFi settings and immediately reboots into the Captive Portal.

---

### II. Multi-Supervisor Status Tracking (Distributed Acknowledgment)

Adapting the sensors to maintain an individualized status flag for every supervisor on their IP list is a highly sophisticated approach to embedded networking. It solves one of the biggest vulnerabilities in decentralized UDP systems: **The Race Condition.**

#### The Problem: The "Global Flag" Vulnerability

In a traditional setup, a sensor has a single boolean flag for an alarm (e.g., `alarm_active = true`).

1. The sensor detects a trigger and sets `alarm_active = true`.
2. Supervisor A pings the sensor, sees the alarm, and commands the sensor to clear the flag (`alarm_active = false`).
3. A fraction of a second later, Supervisor B pings the sensor. Because Supervisor A already cleared the global flag, Supervisor B receives a "normal" status and completely misses the event.

#### The Solution: IP-Specific Status Flags

By modifying the sensor firmware to maintain a matrix of status flags tied to specific IP addresses, you isolate the acknowledgment cycle for every node on the network.

**How it operates:**

1. **The Array:** The sensor maintains an internal list of its known supervisors (e.g., Clock A at `.100`, Clock B at `.101`, Termux at `.102`).
2. **The Trigger:** When a physical event occurs (like the bed scale triggering), the sensor does not set a single flag. Instead, it iterates through its list and flags the event as `UNREAD` for *every* individual IP.
3. **The Isolated Acknowledgment:**
* When Clock A sends a `status ?` ping, the sensor replies with the alarm data and changes *only* Clock A's flag to `READ/CLEARED`.
* The flags for Clock B and Termux remain `UNREAD`.
* When Clock B sends its ping, it successfully receives the exact same alarm data, and its specific flag is subsequently cleared.



#### Why This is Powerful

* **Zero Missed Events:** Network lag, dropped UDP packets, or differing 10-second polling intervals will never result in one clock accidentally clearing an alert before the rest of the house knows about it.
* **Supervisor Ignorance:** The clocks do not need to know about each other. Each supervisor operates under the illusion that it is the only master node on the network, keeping your C++ codebase lightweight.
* **Self-Healing:** If Clock B loses power during an event, its flag on the sensor remains active. When Clock B reboots and resumes its 10-second polling cycle, it will immediately pull the pending alarm, ensuring it is always synchronized with physical reality.
