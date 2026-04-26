# BACnet/IP Device Simulator

A minimal, zero-dependency BACnet/IP simulator written in pure C.  
Runs on **Linux** and **Windows**. Simulates 3 real-looking devices
that appear instantly in YABE with live graphing, COV subscriptions,
and full read/write support.

---

## Devices Simulated

| ID   | Name                  | Objects                                                        |
|------|-----------------------|----------------------------------------------------------------|
| 1001 | HVAC-Controller-01    | Zone Temp (AI), Outside Temp (AI), Damper % (AO), Setpoint (AV), Occupancy (BI), Fan Enable (BO) |
| 2001 | Energy-Meter-01       | Active Power (AI), Voltage (AI), Current (AI), Total Energy (AI), Demand Limit (AV), Alarm (BI) |
| 3001 | AirQuality-Sensor-01  | CO2 (AI), Humidity (AI), Air Pressure (AI), PM2.5 (AI), Ventilation Mode (BV) |

All analog values **drift sinusoidally** so YABE's Plotter shows live graphs.  
Binary values toggle periodically to show state transitions.

---

## Features

- BACnet/IP over UDP (port 47808)
- Multi-device simulation (HVAC, Energy Meter, Air Quality Sensor)
- Full object model with Analog & Binary objects
- WhoIs / I-Am discovery
- ReadProperty & ReadPropertyMultiple
- Object list handling
- WriteProperty support
- Change of Value (COV) subscriptions
- Real-time visualization in YABE
- Dynamic value simulation (sinusoidal + toggling)

---

## Build

### Linux
```bash
gcc -O2 -Wall -o bacnet_sim bacnet_sim.c -lm