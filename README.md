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

## Build

### Linux
```bash
gcc -O2 -Wall -o bacnet_sim bacnet_sim.c -lm
```
Or:
```bash
make
```

### Windows (compile natively with MinGW)
```cmd
gcc -O2 -Wall -o bacnet_sim.exe bacnet_sim.c -lws2_32 -lm
```

### Windows (compile on Linux with MinGW cross-compiler)
```bash
make windows
```

### Windows (MSVC)
```cmd
cl /O2 bacnet_sim.c ws2_32.lib
```

---

## Run

### Linux
```bash
sudo ./bacnet_sim
```
> `sudo` is needed to bind UDP port 47808 on some systems.  
> Alternatively: `sudo setcap cap_net_bind_service=ep ./bacnet_sim`

### Windows
```cmd
bacnet_sim.exe
```
> Allow through Windows Firewall when prompted.

---

## Connect with YABE

1. Start `bacnet_sim` first.
2. Open **YABE**.
3. In the **Devices** panel → right-click **Network View** → **Add UDP**.
4. Leave IP as `0.0.0.0`, port `47808` → **OK**.
5. Click the green **(+)** button → sends **WhoIs**.
6. All 3 devices appear under `Udp:47808`.
7. Expand any device → expand objects → double-click a value to subscribe to the **Subscriptions / Periodic Polling** panel.
8. Select **COV** radio button for change-driven updates, or **Poll** for timed polling.
9. Click **Pause Plotter & Polling** to start/stop live graphs.

### Live Write (WriteProperty)
Right-click any value row in the Subscriptions panel → **Write Value** to send a WriteProperty request. Analog outputs and values accept float; binary outputs and values accept 0/1.

### COV Subscriptions
Right-click any object in the Subscriptions panel → **Subscribe COV** to receive change-of-value notifications. The simulator sends an immediate notification on subscribe and subsequent notifications whenever the value changes by ≥ 0.5 (analog) or any change (binary).

---

## Protocol Support

| Service                     | Status  | Notes                                      |
|-----------------------------|---------|---------------------------------------------|
| WhoIs / I-Am                | ✅ Full  | Unicast reply to requester                 |
| ReadProperty                | ✅ Full  | All standard properties, array indexing    |
| ReadPropertyMultiple        | ✅ Full  | `all` selector (prop=8) supported          |
| WriteProperty               | ✅ Full  | PresentValue + OutOfService, triggers COV  |
| SubscribeCOV                | ✅ Full  | Lifetime, renewal, cancel, immediate notif |
| UnconfirmedCOVNotification  | ✅ Full  | Sent on value change and on write          |
| TimeSynchronization         | ❌       | Not implemented                            |
| ConfirmedCOVNotification    | ❌       | Unconfirmed only                           |
| Segmentation                | ❌       | Not implemented (APDU limit 1476 bytes)    |

### Properties Supported per Object Type

**Device object:** ObjectIdentifier, ObjectName, ObjectType, Description,
SystemStatus, VendorName, VendorIdentifier, ModelName, FirmwareRevision,
ApplicationSoftwareRevision, ProtocolVersion, ProtocolRevision,
MaxAPDULength, Segmentation, APDUTimeout, NumberOfAPDURetries,
DatabaseRevision, ObjectList (with array indexing)

**Analog Input / Output / Value:** ObjectIdentifier, ObjectName, ObjectType,
Description, PresentValue, StatusFlags, EventState, OutOfService, Units,
Reliability, COVIncrement, Resolution  
*AO/AV additionally:* PriorityArray, RelinquishDefault

**Binary Input / Output / Value:** ObjectIdentifier, ObjectName, ObjectType,
Description, PresentValue, StatusFlags, EventState, OutOfService, Polarity,
Reliability, ActiveText, InactiveText  
*BO/BV additionally:* PriorityArray, RelinquishDefault

---

## File Structure

```
bacnet_sim/
├── bacnet_sim.c   # Main simulator — devices, packet handler, COV engine
├── bacnet.h       # Protocol constants, device/object structs
├── encode.h       # BACnet ASN.1 encode/decode helpers
├── Makefile
└── README.md
```

---

## Adding More Devices / Objects

Edit `init_devices()` in `bacnet_sim.c`:

```c
/* Add a 4th device — bump NUM_DEVICES to 4 at top of file first */
BacnetDevice *d = &g_devices[3];
d->device_id   = 4001;
d->vendor_id   = 999;
d->db_revision = 1;
strcpy(d->name,        "MyNewDevice");
strcpy(d->vendor_name, "SimVendor Inc.");
strcpy(d->model_name,  "SIM-NEW-400");
strcpy(d->description, "My custom device");

d->objects[0] = (BacnetObject){
    .instance=0, .type=OBJECT_ANALOG_INPUT,
    .name="My-Sensor", .description="My sensor description",
    .present_value=99.0f, .units=UNITS_PERCENT
};
d->obj_count = 1;
```

Add sinusoidal drift in `tick_values()` if desired:
```c
g_devices[3].objects[0].present_value = (float)(99.0 + 5.0*sin(t*0.3));
```
"# BACnet-IP-Simulator" 
"# BACnet-IP-Simulator" 
