# AI457 Smart Multi-Floor Parking System

A Wokwi-ready IoT/edge demo for 12 parking bays across three floors. Every floor runs its own ESP32 simulation with four HC-SR04 sensors. The ESP32s publish MQTT messages; a Node.js edge server aggregates the live state, detects missing heartbeats, and sends a summary to an entrance ESP32 and a browser dashboard.

## Live Wokwi projects

These saved Wokwi projects are public (Wokwi requires a paid plan for unlisted projects):

- [Floor 1 node](https://wokwi.com/projects/480454166421869569)
- [Floor 2 node](https://wokwi.com/projects/480454191651674113)
- [Floor 3 node](https://wokwi.com/projects/480454207747322881)
- [Entrance display](https://wokwi.com/projects/480454225989420033)

Each floor node and the entrance simulation compiled and connected to `Wokwi-GUEST` and HiveMQ. To get live counts at the entrance, the Node edge server must also be connected to HiveMQ and publishing `harsh_smartpark/summary`.

## 1. Architecture

```text
Floor 1 ESP32 ─┐                              ┌─ Entrance ESP32 + 20x4 LCD
Floor 2 ESP32 ─┼─ MQTT broker ⇄ Node.js edge ┼─ Web dashboard (MQTT/WebSocket)
Floor 3 ESP32 ─┘        status + slots         └─ summary + heartbeat checks
```

Use four Wokwi simulations at once: three separate floor projects (each has its own ESP32 and four slots) and the entrance display project. This matches the one-node-per-floor architecture; each floor diagram is labeled and shows four slots in a row. Create the three floor projects using `wokwi/floor1`, `wokwi/floor2`, and `wokwi/floor3` respectively.

## 2. Wokwi floor nodes

Each folder `wokwi/floor1`, `wokwi/floor2`, and `wokwi/floor3` contains the complete `diagram.json`, `sketch.ino`, and `libraries.txt`. In Wokwi, create an ESP32 project, replace the diagram and sketch with the matching files, and add the listed libraries through Library Manager (or paste `libraries.txt`). Save each project in your Wokwi account so it can be reopened.

### Floor node pin mapping

| Function | Slot 1 | Slot 2 | Slot 3 | Slot 4 |
|---|---:|---:|---:|---:|
| HC-SR04 TRIG | GPIO 5 | GPIO 17 | GPIO 18 | GPIO 19 |
| HC-SR04 ECHO | GPIO 34 | GPIO 35 | GPIO 25 | GPIO 23 |
| Green LED (free) | GPIO 13 | GPIO 26 | GPIO 32 | GPIO 4 |
| Red LED (occupied) | GPIO 14 | GPIO 27 | GPIO 33 | GPIO 16 |
| LED resistor | 220 Ω in series | 220 Ω in series | 220 Ω in series | 220 Ω in series |

LCD1602 I²C: SDA GPIO 21, SCL GPIO 22, VCC 3V3, GND GND. HC-SR04 VCC connects to the board's 5V pin in the simulator; grounds share GND. The selected echo GPIOs are available inputs, and GPIO 23/25 are general-purpose I/O for the later slots. GPIO 4 is used for an LED output; if building a physical ESP32 variant where GPIO 4 is reserved, move that LED to another safe output and update the sketch.

**Floor ID:** floor1 is configured as `FLOOR_ID 1`, floor2 as `FLOOR_ID 2`, floor3 as `FLOOR_ID 3`. If you copy a project, edit this define so every node publishes under its own MQTT topics.

## 3. Wokwi entrance display

`wokwi/entrance` contains the complete 20x4 LCD subscriber project. Paste its diagram and sketch into a separate ESP32 Wokwi project, then add `PubSubClient`, `LiquidCrystal I2C`, and `ArduinoJson`.

| Part | ESP32 pin |
|---|---:|
| 20x4 LCD SDA | GPIO 21 |
| 20x4 LCD SCL | GPIO 22 |
| LCD VCC / GND | 3V3 / GND |

The display shows each floor's free-space count (`0` means full, `X` means offline), the total known-free count, and the recommended floor.

## 4. Edge server

Requires Node.js 18 or newer.

```sh
cd edge-server
npm install
npm start
```

The server connects to HiveMQ's public test broker at `broker.hivemq.com:1883`, listens for all floor slot and status topics, keeps each floor's latest four-slot state in memory, and publishes a retained summary when parking state changes. A floor is marked offline immediately when its MQTT last-will arrives, or after 12 seconds without a heartbeat. Summary free counts exclude offline floors, and the recommended floor is the online floor with the most known-free bays (ties go to the lower floor number).

Open `http://localhost:3000` for the dashboard. Optional environment settings:

```sh
MQTT_ROOT=your_team_prefix MQTT_URL=mqtt://broker.hivemq.com:1883 PORT=3000 npm start
```

If changing `MQTT_ROOT`, change `ROOT` in all three floor sketches, the entrance sketch's `SUMMARY_TOPIC`, and the dashboard's `ROOT` constant to exactly the same prefix. Use a distinctive team prefix on the shared public broker. Public brokers are for demonstrations and have no privacy or availability guarantee; do not send personal data.

## 5. Dashboard

`dashboard/index.html` is a single-page dark dashboard. It connects directly to HiveMQ using secure MQTT-over-WebSocket (`wss://broker.hivemq.com:8884/mqtt`) and subscribes to the summary topic. Chart.js and MQTT.js load from public CDNs, so the dashboard needs internet access. The Node server serves this file at `http://localhost:3000`.

## 6. MQTT topic reference

Default prefix: `harsh_smartpark/`.

| Topic | Publisher | Payload / meaning |
|---|---|---|
| `harsh_smartpark/floor1/slots` | Floor 1 ESP32 | `{"floor":1,"slots":[0,1,0,0],"ts":123}`; 1 occupied, 0 free |
| `harsh_smartpark/floor2/slots` | Floor 2 ESP32 | Same shape with floor 2 |
| `harsh_smartpark/floor3/slots` | Floor 3 ESP32 | Same shape with floor 3 |
| `harsh_smartpark/floorN/status` | Floor N ESP32 | Retained `online` heartbeat every 3 seconds; retained LWT `offline` |
| `harsh_smartpark/summary` | Edge server | `{ts,floors:[{floor,slots,free,occupied,offline}],totalFree,offlineFloors,bestFloor}` |

Slot state is published on a debounced change and refreshed every 5 seconds. `ts` is the node's uptime in milliseconds; the edge summary timestamp is server wall-clock milliseconds.

## 7. Demo script

1. Start the edge server with `npm start` and open the dashboard at `http://localhost:3000`.
2. Run the three floor Wokwi simulations and the entrance Wokwi simulation. Wait for Wi-Fi/MQTT connection messages in the serial monitors and for the dashboard connection badge to turn green.
3. In a floor simulation, click one HC-SR04 sensor and set its Distance slider below 20 cm. Keep it there for about 1 second. The serial monitor should print e.g. `F1-S2 -> OCCUPIED`; the red LED turns on, green turns off, and the local LCD count decreases.
4. The edge server log, dashboard, and entrance LCD should update immediately after the stable sensor change. Move the slider above 20 cm and wait about 1 second to simulate a car leaving. Repeat on other slots and floors.
5. Stop one floor simulation. If the broker detects an unexpected disconnect, its last-will marks the floor offline; otherwise, the 12-second heartbeat timeout does. Start it again to show recovery.
6. For a parking-full demonstration, set all four distances on all three floors below 20 cm and wait about 1 second per slot for the readings to settle. The display/dashboard should say `PARKING FULL`.

In Wokwi, the ultrasonic sensor's adjustable Distance control represents the car. There is no animated car model moving into a bay; the car icon in the dashboard marks occupied bays.

## 8. About

- **Sensor rule:** an HC-SR04 reading below 20 cm means a car is present; no echo or an out-of-range reading is treated as a free space.
- **Debouncing:** the candidate state must remain the same for 450 ms before the slot changes. This filters brief sensor noise while keeping parking updates responsive.
- **Heartbeat:** each floor sends `online` every 3 seconds. The edge marks an explicit MQTT last-will offline immediately, or uses a 12-second timeout if heartbeats stop without a last-will.
- **Edge computing:** the Node.js process near the application receives raw slot states, calculates free counts and the best floor locally, and publishes a compact summary. The entrance display does not need to query every sensor itself.
- **Scalability:** each floor is an independent node/topic. New floors can be added by assigning another floor ID and extending the edge server's floor list/topic parsing.

## 9. Real hardware note

HC-SR04 modules normally use 5V. Their ECHO output can be 5V, which can damage an ESP32 input. Add a resistor voltage divider (for example 1 kΩ from ECHO to the ESP32 input and 2 kΩ from the input to GND) on every ECHO line before a physical build. Use a regulated supply with a shared ground. Check the exact ESP32 board pinout before wiring; this is a simulation project, not a safety-rated parking controller.

## 10. Future improvements

- Reserve bays and identify EV charging spaces.
- Add entry/exit counters, billing, and license-plate recognition.
- Replace public MQTT with a private authenticated broker and TLS.
- Store time-series data in InfluxDB and forecast peak occupancy.
- Add sensor fault diagnostics, calibration, and per-slot maintenance status.
- Expand the server's floor registry and persist state across restarts.
