/*
 * bacnet_sim.c  –  BACnet/IP Device Simulator  (v3)
 *
 * Protocol status:
 *   [✔] WhoIs / I-Am
 *   [✔] ReadProperty (all common properties)
 *   [✔] ReadPropertyMultiple (all-properties + specific props)
 *   [✔] WriteProperty — present-value (analog+binary), out-of-service
 *   [✔] SubscribeCOV — notifications on change, lifetime, cancel
 *   [✔] UnconfirmedCOVNotification on value change and write
 *
 * Build:
 *   gcc -O2 -Wall -o bacnet_sim.exe bacnet_sim.c -lws2_32 -lm  (Windows)
 *   gcc -O2 -Wall -o bacnet_sim bacnet_sim.c -lm               (Linux)
 */

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib,"ws2_32.lib")
   typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/time.h>
#  include <unistd.h>
#  define closesocket close
#  define INVALID_SOCKET (-1)
#  define SOCKET int
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "bacnet.h"
#include "encode.h"

/* -------------------------------------------------------═══════
 *  Extra service codes not in original bacnet.h
 * -------------------------------------------------------═══════ */
#ifndef SERVICE_CONFIRMED_SUBSCRIBE_COV
#  define SERVICE_CONFIRMED_SUBSCRIBE_COV   5
#endif
#ifndef SERVICE_CONFIRMED_WRITE_PROP
#  define SERVICE_CONFIRMED_WRITE_PROP     15
#endif

/* -------------------------------------------------------═══════
 *  COV Subscription table
 * -------------------------------------------------------═══════ */
#define MAX_SUBSCRIPTIONS 64
#define COV_INCREMENT_ANALOG  0.5f   /* trigger notification when change >= this */

typedef struct {
    int      active;
    uint32_t process_id;
    struct sockaddr_in addr;        /* subscriber's IP:port */
    uint16_t obj_type;
    uint32_t obj_inst;
    uint32_t device_id;             /* owning device */
    int      confirmed;             /* 1 = ConfirmedCOV, 0 = UnconfirmedCOV */
    uint32_t lifetime;              /* seconds, 0 = permanent */
    time_t   subscribed_at;
    /* last-sent values for change detection */
    float    last_analog;
    int      last_binary;
} CovSub;

static CovSub  g_subs[MAX_SUBSCRIPTIONS];
static int     g_num_subs = 0;

/* -------------------------------------------------------═══════
 *  Device definitions
 * -------------------------------------------------------═══════ */
#define NUM_DEVICES 3
static BacnetDevice g_devices[NUM_DEVICES];

static void init_devices(void)
{
    /* ── Device 0 : HVAC Controller ──────────────────────────── */
    BacnetDevice *d = &g_devices[0];
    d->device_id   = 1001;
    d->vendor_id   = 999;
    d->db_revision = 1;
    strcpy(d->name,        "HVAC-Controller-01");
    strcpy(d->vendor_name, "SimVendor Inc.");
    strcpy(d->model_name,  "SIM-HVAC-100");
    strcpy(d->description, "Simulated HVAC Controller");

    d->objects[0] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_INPUT,
        .name="Zone-Temperature", .description="Zone temp sensor",
        .present_value=22.5f, .units=UNITS_DEGREES_CELSIUS };
    d->objects[1] = (BacnetObject){
        .instance=1, .type=OBJECT_ANALOG_INPUT,
        .name="Outside-Temperature", .description="Outside air temp",
        .present_value=15.0f, .units=UNITS_DEGREES_CELSIUS };
    d->objects[2] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_OUTPUT,
        .name="Damper-Position", .description="Supply air damper",
        .present_value=45.0f, .units=UNITS_PERCENT };
    d->objects[3] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_VALUE,
        .name="Temp-Setpoint", .description="Temperature setpoint",
        .present_value=21.0f, .units=UNITS_DEGREES_CELSIUS };
    d->objects[4] = (BacnetObject){
        .instance=0, .type=OBJECT_BINARY_INPUT,
        .name="Occupancy-Sensor", .description="Room occupancy",
        .binary_value=true };
    d->objects[5] = (BacnetObject){
        .instance=0, .type=OBJECT_BINARY_OUTPUT,
        .name="Fan-Enable", .description="Supply fan command",
        .binary_value=true };
    d->obj_count = 6;

    /* ── Device 1 : Energy Meter ─────────────────────────────── */
    d = &g_devices[1];
    d->device_id   = 2001;
    d->vendor_id   = 999;
    d->db_revision = 1;
    strcpy(d->name,        "Energy-Meter-01");
    strcpy(d->vendor_name, "SimVendor Inc.");
    strcpy(d->model_name,  "SIM-METER-200");
    strcpy(d->description, "Simulated Energy Meter");

    d->objects[0] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_INPUT,
        .name="Active-Power", .description="Real power consumption",
        .present_value=12500.0f, .units=UNITS_WATTS };
    d->objects[1] = (BacnetObject){
        .instance=1, .type=OBJECT_ANALOG_INPUT,
        .name="Voltage-L1", .description="Phase 1 voltage",
        .present_value=230.2f, .units=UNITS_VOLTS };
    d->objects[2] = (BacnetObject){
        .instance=2, .type=OBJECT_ANALOG_INPUT,
        .name="Current-L1", .description="Phase 1 current",
        .present_value=18.3f, .units=UNITS_AMPERES };
    d->objects[3] = (BacnetObject){
        .instance=3, .type=OBJECT_ANALOG_INPUT,
        .name="Total-Energy", .description="Cumulative energy",
        .present_value=456789.0f, .units=UNITS_NO_UNITS };
    d->objects[4] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_VALUE,
        .name="Demand-Limit", .description="Peak demand limit",
        .present_value=15000.0f, .units=UNITS_WATTS };
    d->objects[5] = (BacnetObject){
        .instance=0, .type=OBJECT_BINARY_INPUT,
        .name="Alarm-Status", .description="Meter alarm flag",
        .binary_value=false };
    d->obj_count = 6;

    /* ── Device 2 : Air Quality Sensor ───────────────────────── */
    d = &g_devices[2];
    d->device_id   = 3001;
    d->vendor_id   = 999;
    d->db_revision = 1;
    strcpy(d->name,        "AirQuality-Sensor-01");
    strcpy(d->vendor_name, "SimVendor Inc.");
    strcpy(d->model_name,  "SIM-AQS-300");
    strcpy(d->description, "Simulated Air Quality Sensor");

    d->objects[0] = (BacnetObject){
        .instance=0, .type=OBJECT_ANALOG_INPUT,
        .name="CO2-Level", .description="CO2 concentration ppm",
        .present_value=650.0f, .units=UNITS_NO_UNITS };
    d->objects[1] = (BacnetObject){
        .instance=1, .type=OBJECT_ANALOG_INPUT,
        .name="Humidity", .description="Relative humidity",
        .present_value=48.5f, .units=UNITS_PERCENT };
    d->objects[2] = (BacnetObject){
        .instance=2, .type=OBJECT_ANALOG_INPUT,
        .name="Air-Pressure", .description="Barometric pressure",
        .present_value=101325.0f, .units=UNITS_PASCALS };
    d->objects[3] = (BacnetObject){
        .instance=3, .type=OBJECT_ANALOG_INPUT,
        .name="PM25-Level", .description="Particulate matter 2.5um",
        .present_value=12.3f, .units=UNITS_NO_UNITS };
    d->objects[4] = (BacnetObject){
        .instance=0, .type=OBJECT_BINARY_VALUE,
        .name="Ventilation-Mode", .description="Normal/High vent mode",
        .binary_value=false };
    d->obj_count = 5;
}

/* -------------------------------------------------------═══════
 *  Present-value drift (makes YABE graphs interesting)
 * -------------------------------------------------------═══════ */
static void tick_values(void)
{
    static unsigned tick = 0;
    tick++;
    double t = tick * 0.05;

    /* HVAC */
    g_devices[0].objects[0].present_value = (float)(22.5 + 1.5*sin(t));
    g_devices[0].objects[1].present_value = (float)(15.0 + 3.0*sin(t*0.3));
    g_devices[0].objects[2].present_value = (float)(45.0 + 20.0*sin(t*0.7));

    /* Energy meter */
    g_devices[1].objects[0].present_value = (float)(12500.0 + 2000.0*sin(t*0.4));
    g_devices[1].objects[1].present_value = (float)(230.2   + 0.5*sin(t*1.1));
    g_devices[1].objects[2].present_value = (float)(18.3    + 2.0*sin(t*0.9));

    /* AQ sensor */
    g_devices[2].objects[0].present_value = (float)(650.0 + 100.0*sin(t*0.2));
    g_devices[2].objects[1].present_value = (float)(48.5  + 5.0*sin(t*0.6));
    g_devices[2].objects[3].present_value = (float)(12.3  + 3.0*fabs(sin(t*0.8)));

    /* Toggle binary values slowly so YABE shows transitions */
    if ((tick % 100) == 0)
        g_devices[0].objects[4].binary_value ^= 1;  /* Occupancy */
    if ((tick % 150) == 0)
        g_devices[1].objects[5].binary_value ^= 1;  /* Alarm */
}

/* -------------------------------------------------------═══════
 *  Look up a device/object by type+instance
 * -------------------------------------------------------═══════ */
/* Look up object by type+instance, scoped to device_id (0 = any device) */
static BacnetObject *find_object_in(uint16_t type, uint32_t inst,
                                    uint32_t device_id,
                                    BacnetDevice **dev_out)
{
    for (int d = 0; d < NUM_DEVICES; d++) {
        if (device_id && g_devices[d].device_id != device_id) continue;
        for (int o = 0; o < g_devices[d].obj_count; o++) {
            if (g_devices[d].objects[o].type     == type &&
                g_devices[d].objects[o].instance == inst) {
                if (dev_out) *dev_out = &g_devices[d];
                return &g_devices[d].objects[o];
            }
        }
    }
    return NULL;
}

static BacnetObject *find_object(uint16_t type, uint32_t inst,
                                 BacnetDevice **dev_out)
{
    return find_object_in(type, inst, 0, dev_out);
}

/* -------------------------------------------------------═══════
 *  Packet builder helpers
 * -------------------------------------------------------═══════ */
static int build_bvlc_npdu_header(uint8_t *buf, int broadcast)
{
    int len = 0;
    buf[len++] = BVLC_TYPE;
    buf[len++] = broadcast ? BVLC_ORIGINAL_BROADCAST_NPDU
                           : BVLC_ORIGINAL_UNICAST_NPDU;
    buf[len++] = 0;   /* length placeholder */
    buf[len++] = 0;
    buf[len++] = NPDU_VERSION;
    buf[len++] = NPDU_NO_APDU_MSG;
    return len;
}

static void fix_bvlc_length(uint8_t *buf, int total_len)
{
    buf[2] = (uint8_t)(total_len >> 8);
    buf[3] = (uint8_t)total_len;
}

/* -------------------------------------------------------═══════
 *  I-Am builder  (unicast or broadcast)
 * -------------------------------------------------------═══════ */
static int build_i_am(uint8_t *buf, const BacnetDevice *dev, int broadcast)
{
    int len = build_bvlc_npdu_header(buf, broadcast);
    buf[len++] = PDU_TYPE_UNCONFIRMED;
    buf[len++] = SERVICE_UNCONFIRMED_I_AM;
    len += encode_app_object_id(buf + len, OBJECT_DEVICE, dev->device_id);
    len += encode_app_unsigned(buf + len, 1476);          /* maxAPDU */
    len += encode_app_enumerated(buf + len, 3);           /* no segmentation */
    len += encode_app_unsigned(buf + len, dev->vendor_id);
    fix_bvlc_length(buf, len);
    return len;
}

/* -------------------------------------------------------═══════
 *  SimpleACK builder
 * -------------------------------------------------------═══════ */
static int build_simple_ack(uint8_t *buf, uint8_t invoke_id, uint8_t svc)
{
    int len = build_bvlc_npdu_header(buf, 0);
    buf[len++] = PDU_TYPE_SIMPLE_ACK;
    buf[len++] = invoke_id;
    buf[len++] = svc;
    fix_bvlc_length(buf, len);
    return len;
}

/* -------------------------------------------------------═══════
 *  Error response
 * -------------------------------------------------------═══════ */
static int build_error(uint8_t *buf, uint8_t invoke_id,
                        uint8_t service, uint8_t ec, uint8_t ep)
{
    int len = build_bvlc_npdu_header(buf, 0);
    buf[len++] = PDU_TYPE_ERROR;
    buf[len++] = invoke_id;
    buf[len++] = service;
    buf[len++] = 0x09; buf[len++] = ec;
    buf[len++] = 0x19; buf[len++] = ep;
    fix_bvlc_length(buf, len);
    return len;
}

/* -------------------------------------------------------═══════
 *  Property value encoder
 * -------------------------------------------------------═══════ */
static int encode_property_value(uint8_t *buf,
                                  const BacnetDevice *dev,
                                  const BacnetObject *obj,
                                  int is_device,
                                  uint32_t prop_id,
                                  int32_t  array_index)
{
    int len = 0;
    int i;

    if (is_device) {
        switch (prop_id) {
        case PROP_OBJECT_IDENTIFIER:
            len += encode_app_object_id(buf+len, OBJECT_DEVICE, dev->device_id);
            break;
        case PROP_OBJECT_NAME:
            len += encode_app_character_string(buf+len, dev->name);
            break;
        case PROP_OBJECT_TYPE:
            len += encode_app_enumerated(buf+len, OBJECT_DEVICE);
            break;
        case PROP_DESCRIPTION:
            len += encode_app_character_string(buf+len, dev->description);
            break;
        case PROP_SYSTEM_STATUS:
            len += encode_app_enumerated(buf+len, 0);
            break;
        case PROP_VENDOR_NAME:
            len += encode_app_character_string(buf+len, dev->vendor_name);
            break;
        case PROP_VENDOR_IDENTIFIER:
            len += encode_app_unsigned(buf+len, dev->vendor_id);
            break;
        case PROP_MODEL_NAME:
            len += encode_app_character_string(buf+len, dev->model_name);
            break;
        case PROP_FIRMWARE_REVISION:
            len += encode_app_character_string(buf+len, "2.0.0");
            break;
        case PROP_APP_SOFTWARE_REV:
            len += encode_app_character_string(buf+len, "2.0.0");
            break;
        case PROP_PROTOCOL_VERSION:
            len += encode_app_unsigned(buf+len, 1);
            break;
        case PROP_PROTOCOL_REVISION:
            len += encode_app_unsigned(buf+len, 14);
            break;
        case PROP_MAX_APDU_LENGTH:
            len += encode_app_unsigned(buf+len, 1476);
            break;
        case PROP_SEGMENTATION:
            len += encode_app_enumerated(buf+len, 3);
            break;
        case PROP_APDU_TIMEOUT:
            len += encode_app_unsigned(buf+len, 3000);
            break;
        case PROP_NUM_APDU_RETRIES:
            len += encode_app_unsigned(buf+len, 3);
            break;
        case PROP_DATABASE_REVISION:
            len += encode_app_unsigned(buf+len, dev->db_revision);
            break;
        case PROP_OBJECT_LIST:
            if (array_index == 0) {
                len += encode_app_unsigned(buf+len,
                           (uint32_t)(1 + dev->obj_count));
            } else if (array_index > 0) {
                int idx = array_index - 1;
                if (idx == 0) {
                    len += encode_app_object_id(buf+len,
                               OBJECT_DEVICE, dev->device_id);
                } else if (idx - 1 < dev->obj_count) {
                    int oi = idx - 1;
                    len += encode_app_object_id(buf+len,
                               dev->objects[oi].type,
                               dev->objects[oi].instance);
                } else {
                    return 0;
                }
            } else {
                len += encode_app_object_id(buf+len,
                           OBJECT_DEVICE, dev->device_id);
                for (i = 0; i < dev->obj_count; i++) {
                    len += encode_app_object_id(buf+len,
                               dev->objects[i].type,
                               dev->objects[i].instance);
                }
            }
            break;
        default:
            return 0;
        }
    } else {
        bool is_binary = (obj->type == OBJECT_BINARY_INPUT  ||
                          obj->type == OBJECT_BINARY_OUTPUT ||
                          obj->type == OBJECT_BINARY_VALUE);

        switch (prop_id) {
        case PROP_OBJECT_IDENTIFIER:
            len += encode_app_object_id(buf+len, obj->type, obj->instance);
            break;
        case PROP_OBJECT_NAME:
            len += encode_app_character_string(buf+len, obj->name);
            break;
        case PROP_OBJECT_TYPE:
            len += encode_app_enumerated(buf+len, obj->type);
            break;
        case PROP_DESCRIPTION:
            len += encode_app_character_string(buf+len, obj->description);
            break;
        case PROP_PRESENT_VALUE:
            if (is_binary)
                len += encode_app_enumerated(buf+len, obj->binary_value ? 1 : 0);
            else
                len += encode_app_real(buf+len, obj->present_value);
            break;
        case PROP_STATUS_FLAGS:
            len += encode_status_flags(buf+len, 0, 0, 0, obj->out_of_service);
            break;
        case PROP_EVENT_STATE:
            len += encode_app_enumerated(buf+len, 0);
            break;
        case PROP_OUT_OF_SERVICE:
            len += encode_app_boolean(buf+len, obj->out_of_service);
            break;
        case PROP_UNITS:
            if (!is_binary)
                len += encode_app_enumerated(buf+len, obj->units);
            else return 0;
            break;
        case PROP_POLARITY:
            if (is_binary)
                len += encode_app_enumerated(buf+len, obj->polarity ? 1 : 0);
            else return 0;
            break;
        case PROP_RELIABILITY:
            len += encode_app_enumerated(buf+len, 0);
            break;
        case PROP_ACTIVE_TEXT:
            if (is_binary) len += encode_app_character_string(buf+len, "Active");
            else return 0;
            break;
        case PROP_INACTIVE_TEXT:
            if (is_binary) len += encode_app_character_string(buf+len, "Inactive");
            else return 0;
            break;
        case PROP_RELINQUISH_DEFAULT:
            if (is_binary) len += encode_app_enumerated(buf+len, 0);
            else           len += encode_app_real(buf+len, 0.0f);
            break;
        case PROP_PRIORITY_ARRAY:
            if (obj->type == OBJECT_ANALOG_OUTPUT ||
                obj->type == OBJECT_BINARY_OUTPUT) {
                /* 16 null entries — BACnet NULL tag = 0x00 */
                for (i = 0; i < 16; i++)
                    buf[len++] = 0x00;
            } else return 0;
            break;
        case PROP_COV_INCREMENT:
            if (!is_binary) len += encode_app_real(buf+len, COV_INCREMENT_ANALOG);
            else return 0;
            break;
        case PROP_RESOLUTION:
            if (!is_binary) len += encode_app_real(buf+len, 0.1f);
            else return 0;
            break;
        default:
            return 0;
        }
    }
    return len;
}

/* -------------------------------------------------------═══════
 *  ReadProperty ACK
 * -------------------------------------------------------═══════ */
static int build_read_property_ack(uint8_t *buf,
                                    uint8_t invoke_id,
                                    const BacnetDevice *dev,
                                    const BacnetObject *obj,
                                    int is_device,
                                    uint16_t obj_type, uint32_t obj_inst,
                                    uint32_t prop_id, int32_t array_index)
{
    int len = build_bvlc_npdu_header(buf, 0);
    buf[len++] = PDU_TYPE_COMPLEX_ACK;
    buf[len++] = invoke_id;
    buf[len++] = SERVICE_CONFIRMED_READ_PROP;

    len += encode_context_object_id(buf+len, 0, obj_type, obj_inst);
    len += encode_context_unsigned(buf+len, 1, prop_id);
    if (array_index >= 0)
        len += encode_context_unsigned(buf+len, 2, (uint32_t)array_index);
    len += encode_opening_tag(buf+len, 3);

    int vlen = encode_property_value(buf+len, dev, obj, is_device,
                                     prop_id, array_index);
    if (vlen == 0) return -1;
    len += vlen;

    len += encode_closing_tag(buf+len, 3);
    fix_bvlc_length(buf, len);
    return len;
}

/* -------------------------------------------------------═══════
 *  COV Notification builder
 *
 *  UnconfirmedCOVNotification wire layout:
 *    [0] subscriberProcessIdentifier  unsigned
 *    [1] initiatingDeviceIdentifier   object-id
 *    [2] monitoredObjectIdentifier    object-id
 *    [3] timeRemaining                unsigned
 *    [4] listOfValues  opening
 *          [0] propertyIdentifier  context-unsigned  (PRESENT_VALUE=85)
 *          [2] propertyValue opening
 *                <encoded value>
 *          [2] propertyValue closing
 *          [0] propertyIdentifier  context-unsigned  (STATUS_FLAGS=111)
 *          [2] propertyValue opening
 *                <status flags>
 *          [2] propertyValue closing
 *    [4] listOfValues closing
 * -------------------------------------------------------═══════ */
static int build_cov_notification(uint8_t *buf,
                                   const CovSub *sub,
                                   const BacnetDevice *dev,
                                   const BacnetObject *obj)
{
    int len = build_bvlc_npdu_header(buf, 0 /* unicast */);
    buf[len++] = PDU_TYPE_UNCONFIRMED;
    buf[len++] = SERVICE_UNCONFIRMED_COV_NOTIF;  /* 2 */

    /* [0] subscriberProcessIdentifier */
    len += encode_context_unsigned(buf+len, 0, sub->process_id);

    /* [1] initiatingDeviceIdentifier */
    len += encode_context_object_id(buf+len, 1, OBJECT_DEVICE, dev->device_id);

    /* [2] monitoredObjectIdentifier */
    len += encode_context_object_id(buf+len, 2, sub->obj_type, sub->obj_inst);

    /* [3] timeRemaining (seconds) */
    uint32_t remaining = 0;
    if (sub->lifetime > 0) {
        time_t elapsed = time(NULL) - sub->subscribed_at;
        remaining = (sub->lifetime > (uint32_t)elapsed)
                    ? sub->lifetime - (uint32_t)elapsed : 0;
    }
    len += encode_context_unsigned(buf+len, 3, remaining);

    /* [4] listOfValues opening */
    buf[len++] = 0x4E;

    bool is_bin = (obj->type == OBJECT_BINARY_INPUT  ||
                   obj->type == OBJECT_BINARY_OUTPUT ||
                   obj->type == OBJECT_BINARY_VALUE);

    /* ── entry 1: present-value ── */
    len += encode_context_unsigned(buf+len, 0, PROP_PRESENT_VALUE); /* [0] propId  */
    buf[len++] = 0x2E;                                               /* [2] opening */
    if (is_bin)
        len += encode_app_enumerated(buf+len, obj->binary_value ? 1 : 0);
    else
        len += encode_app_real(buf+len, obj->present_value);
    buf[len++] = 0x2F;                                               /* [2] closing */

    /* ── entry 2: status-flags  ── */
    len += encode_context_unsigned(buf+len, 0, PROP_STATUS_FLAGS);   /* [0] propId  */
    buf[len++] = 0x2E;                                               /* [2] opening */
    len += encode_status_flags(buf+len, 0, 0, 0, obj->out_of_service);
    buf[len++] = 0x2F;                                               /* [2] closing */

    /* [4] listOfValues closing */
    buf[len++] = 0x4F;

    fix_bvlc_length(buf, len);
    return len;
}

/* -------------------------------------------------------═══════
 *  Send COV notifications for all active subscriptions
 *  Called after tick_values() updates present-values
 * -------------------------------------------------------═══════ */
static void send_cov_notifications(SOCKET sock)
{
    uint8_t  buf[512];
    time_t   now = time(NULL);

    for (int s = 0; s < g_num_subs; s++) {
        CovSub *sub = &g_subs[s];
        if (!sub->active) continue;

        /* Expire subscription? */
        if (sub->lifetime > 0) {
            time_t elapsed = now - sub->subscribed_at;
            if ((uint32_t)elapsed >= sub->lifetime) {
                printf("[COV] Subscription %d expired (device %u)\n",
                       s, sub->device_id);
                sub->active = 0;
                continue;
            }
        }

        /* Find the object scoped to the subscribed device */
        BacnetDevice *dev = NULL;
        BacnetObject *obj = find_object_in(sub->obj_type, sub->obj_inst,
                                           sub->device_id, &dev);
        if (!obj || !dev) { sub->active = 0; continue; }

        /* Check if value changed enough */
        bool is_bin = (obj->type == OBJECT_BINARY_INPUT  ||
                       obj->type == OBJECT_BINARY_OUTPUT ||
                       obj->type == OBJECT_BINARY_VALUE);
        int changed = 0;
        if (is_bin) {
            changed = (obj->binary_value != sub->last_binary);
        } else {
            changed = (fabsf(obj->present_value - sub->last_analog)
                       >= COV_INCREMENT_ANALOG);
        }
        if (!changed) continue;

        /* Build and send notification */
        int len = build_cov_notification(buf, sub, dev, obj);
        sendto(sock, (char*)buf, len, 0,
               (struct sockaddr*)&sub->addr, sizeof(sub->addr));

        /* Update last-sent value */
        sub->last_analog  = obj->present_value;
        sub->last_binary  = obj->binary_value;

        printf("[TX] COV notify  dev=%u  obj type=%u inst=%u  val=%.2f\n",
               dev->device_id, sub->obj_type, sub->obj_inst,
               is_bin ? (float)obj->binary_value : obj->present_value);
    }
}

/* -------------------------------------------------------═══════
 *  SubscribeCOV handler
 *
 *  Request wire format (confirmed, service 5):
 *    [0] subscriberProcessIdentifier  context-unsigned
 *    [1] monitoredObjectIdentifier    context-object-id
 *    [2] issueConfirmedNotifications  context-bool      (optional, unsubscribe if absent)
 *    [3] lifetime                     context-unsigned  (optional, 0=permanent)
 * -------------------------------------------------------═══════ */
static void handle_subscribe_cov(SOCKET sock,
                                  const uint8_t *pkt, int pkt_len,
                                  int data_off,
                                  uint8_t invoke_id,
                                  const struct sockaddr_in *from)
{
    uint8_t resp[64];

    /* Helper macro reuse */
#   define READ_CTX_UINT(buf_, off_, max_, val_, ok_) \
    do { \
        uint8_t _tb = (buf_)[(off_)++]; \
        uint8_t _lv = _tb & 0x07; \
        if (_lv == 5) { _lv = (buf_)[(off_)++]; } \
        (val_) = 0; (ok_) = (_lv >= 1 && _lv <= 4 && (int)((off_)+_lv) <= (max_)); \
        if (ok_) { for (uint8_t _i=0;_i<_lv;_i++) (val_)=((val_)<<8)|(buf_)[(off_)++]; } \
    } while(0)

    /* [0] subscriberProcessIdentifier */
    uint32_t proc_id = 0; int ok = 0;
    READ_CTX_UINT(pkt, data_off, pkt_len, proc_id, ok);
    if (!ok) goto send_ack;

    /* [1] monitoredObjectIdentifier – context tag 1, 4 bytes */
    if (data_off + 5 > pkt_len) goto send_ack;
    data_off++;   /* skip tag byte 0x1C */
    uint32_t raw_oid = ((uint32_t)pkt[data_off]   << 24) |
                       ((uint32_t)pkt[data_off+1]  << 16) |
                       ((uint32_t)pkt[data_off+2]  <<  8) |
                        (uint32_t)pkt[data_off+3];
    data_off += 4;
    uint16_t obj_type = (uint16_t)(raw_oid >> 22);
    uint32_t obj_inst = raw_oid & 0x3FFFFF;

    /* [2] issueConfirmedNotifications (optional) */
    int confirmed = 0;
    if (data_off < pkt_len && (pkt[data_off] & 0xF8) == 0x28) {
        /* tag 2, context */
        uint32_t cv = 0;
        READ_CTX_UINT(pkt, data_off, pkt_len, cv, ok);
        confirmed = (int)cv;
    }

    /* [3] lifetime (optional, 0 = permanent) */
    uint32_t lifetime = 0;
    if (data_off < pkt_len && (pkt[data_off] & 0xF8) == 0x38) {
        uint32_t lv = 0;
        READ_CTX_UINT(pkt, data_off, pkt_len, lv, ok);
        lifetime = lv;
    }

    /* Find the owning device */
    BacnetDevice *dev = NULL;
    BacnetObject *obj = find_object(obj_type, obj_inst, &dev);

    if (!obj || !dev) {
        printf("[COV] Subscribe for unknown obj type=%u inst=%u – error\n",
               obj_type, obj_inst);
        int elen = build_error(resp, invoke_id,
                               SERVICE_CONFIRMED_SUBSCRIBE_COV, 1, 31);
        sendto(sock, (char*)resp, elen, 0,
               (struct sockaddr*)from, sizeof(*from));
        return;
    }

    /* lifetime==0 with absent [2] means CANCEL */
    if (lifetime == 0 && data_off < pkt_len &&
        (pkt[data_off-1] & 0xF8) != 0x38) {
        /* Cancel – remove matching subscription */
        for (int s = 0; s < g_num_subs; s++) {
            CovSub *sub = &g_subs[s];
            if (sub->active &&
                sub->process_id == proc_id &&
                sub->obj_type   == obj_type &&
                sub->obj_inst   == obj_inst &&
                memcmp(&sub->addr, from, sizeof(*from)) == 0) {
                sub->active = 0;
                printf("[COV] Cancelled subscription %d\n", s);
            }
        }
        goto send_ack;
    }

    /* Find a free slot (or reuse existing matching one) */
    CovSub *slot = NULL;
    for (int s = 0; s < g_num_subs; s++) {
        CovSub *sub = &g_subs[s];
        if (sub->active &&
            sub->process_id == proc_id &&
            sub->obj_type   == obj_type &&
            sub->obj_inst   == obj_inst &&
            memcmp(&sub->addr, from, sizeof(*from)) == 0) {
            slot = sub;   /* renew */
            break;
        }
    }
    if (!slot) {
        if (g_num_subs < MAX_SUBSCRIPTIONS) {
            slot = &g_subs[g_num_subs++];
        } else {
            /* reclaim first expired */
            for (int s = 0; s < g_num_subs; s++) {
                if (!g_subs[s].active) { slot = &g_subs[s]; break; }
            }
        }
    }

    if (slot) {
        slot->active        = 1;
        slot->process_id    = proc_id;
        slot->addr          = *from;
        slot->obj_type      = obj_type;
        slot->obj_inst      = obj_inst;
        slot->device_id     = dev->device_id;
        slot->confirmed     = confirmed;
        slot->lifetime      = lifetime;
        slot->subscribed_at = time(NULL);
        slot->last_analog   = obj->present_value;
        slot->last_binary   = obj->binary_value;

        printf("[COV] Subscribed: proc=%u  dev=%u  obj type=%u inst=%u  "
               "confirmed=%d  lifetime=%us\n",
               proc_id, dev->device_id, obj_type, obj_inst,
               confirmed, lifetime);

        /* Send an immediate notification so YABE shows value right away */
        uint8_t notif[512];
        int nlen = build_cov_notification(notif, slot, dev, obj);
        sendto(sock, (char*)notif, nlen, 0,
               (struct sockaddr*)from, sizeof(*from));
    }

send_ack:;
    int alen = build_simple_ack(resp, invoke_id, SERVICE_CONFIRMED_SUBSCRIBE_COV);
    sendto(sock, (char*)resp, alen, 0,
           (struct sockaddr*)from, sizeof(*from));
}

/* -------------------------------------------------------═══════
 *  Packet dispatcher
 * -------------------------------------------------------═══════ */
static void dispatch(SOCKET sock,
                     const uint8_t *pkt, int pkt_len,
                     const struct sockaddr_in *from)
{
    if (pkt_len < 6) return;
    if (pkt[0] != BVLC_TYPE) return;

    int npdu_off = 4;
    if (pkt[npdu_off] != NPDU_VERSION) return;
    int npdu_ctrl = pkt[npdu_off + 1];
    int apdu_off  = npdu_off + 2;

    /* Skip NPDU routing */
    if (npdu_ctrl & 0x20) {
        if (apdu_off + 3 > pkt_len) return;
        apdu_off += 3 + pkt[apdu_off + 2];
        apdu_off++;
    }
    if (npdu_ctrl & 0x08) {
        if (apdu_off + 3 > pkt_len) return;
        apdu_off += 3 + pkt[apdu_off + 2];
    }
    if (npdu_ctrl & 0x80) return;
    if (apdu_off >= pkt_len) return;

    uint8_t apdu_type = pkt[apdu_off] & 0xF0;
    uint8_t resp[1500];
    int rlen;

    /* ── Unconfirmed ─────────────────────────────────────────── */
    if (apdu_type == PDU_TYPE_UNCONFIRMED) {
        uint8_t svc = pkt[apdu_off + 1];

        if (svc == SERVICE_UNCONFIRMED_WHO_IS) {
            printf("[RX] WhoIs from %s\n", inet_ntoa(from->sin_addr));

            /*
             * KEY FIX: Send I-Am as UNICAST directly back to the requester.
             * Broadcasting to 255.255.255.255 on localhost may not loop back
             * to the YABE process on the same machine (OS-dependent).
             * Unicast is guaranteed to arrive.
             */
            for (int d = 0; d < NUM_DEVICES; d++) {
                rlen = build_i_am(resp, &g_devices[d], 0 /* unicast */);
                sendto(sock, (char*)resp, rlen, 0,
                       (struct sockaddr*)from, sizeof(*from));
                printf("[TX] I-Am (unicast) device %u → %s\n",
                       g_devices[d].device_id, inet_ntoa(from->sin_addr));
            }
        }
        return;
    }

    /* ── Confirmed ───────────────────────────────────────────── */
    if (apdu_type == PDU_TYPE_CONFIRMED) {
        uint8_t flags     = pkt[apdu_off];
        uint8_t max_resp  = pkt[apdu_off + 1];
        uint8_t invoke_id = pkt[apdu_off + 2];
        uint8_t svc       = pkt[apdu_off + 3];
        int     data_off  = apdu_off + 4;
        (void)flags; (void)max_resp;

        /* ── SubscribeCOV ──────────────────────────────────────── */
        if (svc == SERVICE_CONFIRMED_SUBSCRIBE_COV) {
            handle_subscribe_cov(sock, pkt, pkt_len,
                                 data_off, invoke_id, from);
            return;
        }

        /* ── ReadProperty ──────────────────────────────────────── */
        if (svc == SERVICE_CONFIRMED_READ_PROP) {
            if (data_off + 6 > pkt_len) return;

#           define SKIP_TAG_AND_READ_BYTES(buf_,off_,max_,out_,ok_) \
            do { \
                uint8_t _tb=(buf_)[(off_)++]; \
                uint8_t _tn=(_tb>>4)&0x0F; \
                uint8_t _lv=_tb&0x07; \
                if(_tn==0x0F)(off_)++; \
                uint32_t _l=_lv; \
                if(_lv==5){_l=(buf_)[(off_)++];} \
                else if(_lv==6){_l=(buf_)[(off_)++]<<8;_l|=(buf_)[(off_)++];} \
                (out_)=0; \
                (ok_)=(_l>=1&&_l<=4&&(int)((off_)+_l)<=(max_)); \
                if(ok_){for(uint32_t _i=0;_i<_l;_i++)(out_)=((out_)<<8)|(buf_)[(off_)++];} \
            }while(0)

            uint32_t raw_oid=0; int ok0=0;
            SKIP_TAG_AND_READ_BYTES(pkt,data_off,pkt_len,raw_oid,ok0);
            if(!ok0) return;
            uint16_t obj_type=(uint16_t)(raw_oid>>22);
            uint32_t obj_inst=raw_oid&0x3FFFFF;

            uint32_t prop_id=0; int ok1=0;
            SKIP_TAG_AND_READ_BYTES(pkt,data_off,pkt_len,prop_id,ok1);
            if(!ok1) return;

            printf("[RX] ReadProperty type=%u inst=%u prop=%u\n",
                   obj_type, obj_inst, prop_id);

            int32_t array_index=-1;
            if (data_off < pkt_len) {
                uint8_t peek=pkt[data_off];
                if ((peek & 0xF8) == 0x28) {
                    uint32_t ai=0; int ok2=0;
                    SKIP_TAG_AND_READ_BYTES(pkt,data_off,pkt_len,ai,ok2);
                    if(ok2) array_index=(int32_t)ai;
                }
            }

            for (int d=0; d<NUM_DEVICES; d++) {
                BacnetDevice *dev=&g_devices[d];
                if (obj_type==OBJECT_DEVICE && obj_inst==dev->device_id) {
                    rlen=build_read_property_ack(resp,invoke_id,dev,NULL,1,
                                                 obj_type,obj_inst,prop_id,array_index);
                    if(rlen<0) rlen=build_error(resp,invoke_id,svc,2,32);
                    sendto(sock,(char*)resp,rlen,0,(struct sockaddr*)from,sizeof(*from));
                    return;
                }
                for (int o=0; o<dev->obj_count; o++) {
                    BacnetObject *obj2=&dev->objects[o];
                    if (obj2->type==obj_type && obj2->instance==obj_inst) {
                        rlen=build_read_property_ack(resp,invoke_id,dev,obj2,0,
                                                     obj_type,obj_inst,prop_id,array_index);
                        if(rlen<0) rlen=build_error(resp,invoke_id,svc,2,32);
                        sendto(sock,(char*)resp,rlen,0,(struct sockaddr*)from,sizeof(*from));
                        return;
                    }
                }
            }
            rlen=build_error(resp,invoke_id,svc,1,31);
            sendto(sock,(char*)resp,rlen,0,(struct sockaddr*)from,sizeof(*from));
        }

        /* ── ReadPropertyMultiple ──────────────────────────────── */
        else if (svc == SERVICE_CONFIRMED_READ_PROP_MULT) {
            printf("[RX] ReadPropertyMultiple invoke=%u\n", invoke_id);

            int rlen2 = build_bvlc_npdu_header(resp, 0);
            resp[rlen2++] = PDU_TYPE_COMPLEX_ACK;
            resp[rlen2++] = invoke_id;
            resp[rlen2++] = SERVICE_CONFIRMED_READ_PROP_MULT;

            int req = data_off;

            while (req < pkt_len - 1) {
                if (req >= pkt_len) break;
                uint8_t otag = pkt[req++];
                uint8_t olen = otag & 0x07;
                if (olen == 5) { olen = pkt[req++]; }
                if (olen != 4 || req + 4 > pkt_len) break;
                uint32_t raw = 0;
                for (int b=0; b<4; b++) raw=(raw<<8)|pkt[req++];
                uint16_t rtype=(uint16_t)(raw>>22);
                uint32_t rinst=raw&0x3FFFFF;

                BacnetDevice *fdev=NULL; BacnetObject *fobj=NULL; int fis_dev=0;
                for (int d=0; d<NUM_DEVICES&&!fdev; d++) {
                    if (rtype==OBJECT_DEVICE && rinst==g_devices[d].device_id) {
                        fdev=&g_devices[d]; fis_dev=1;
                    } else {
                        for (int o=0; o<g_devices[d].obj_count; o++) {
                            if (g_devices[d].objects[o].type==rtype &&
                                g_devices[d].objects[o].instance==rinst) {
                                fdev=&g_devices[d];
                                fobj=&g_devices[d].objects[o];
                                break;
                            }
                        }
                    }
                }

                if (req>=pkt_len) break;
                /* Accept any opening tag (lower nibble = 0x0E) */
                if ((pkt[req] & 0x0F) != 0x0E) {
                    printf("[RPM] Expected opening tag, got 0x%02X\n", pkt[req]);
                    break;
                }
                req++;

                uint32_t props[64]; int nprops=0; int read_all=0;
                while (req < pkt_len) {
                    uint8_t pb=pkt[req];
                    if (pb==0x1F) { req++; break; }
                    req++;
                    uint8_t plen2=pb&0x07;
                    if (plen2==5) { plen2=pkt[req++]; }
                    uint32_t pid=0;
                    for (int b=0; b<plen2&&b<4; b++) pid=(pid<<8)|pkt[req++];
                    printf("  RPM prop=%u\n", pid);
                    if (pid==8||pid==0xFFFF||pid==4194303U) {
                        read_all=1;
                    } else if (nprops<64) {
                        props[nprops++]=pid;
                    }
                }

                if (read_all) {
                    nprops=0;
                    if (fis_dev) {
                        static const uint32_t dp[]={
                            PROP_OBJECT_IDENTIFIER,PROP_OBJECT_NAME,
                            PROP_OBJECT_TYPE,PROP_DESCRIPTION,
                            PROP_SYSTEM_STATUS,PROP_VENDOR_NAME,
                            PROP_VENDOR_IDENTIFIER,PROP_MODEL_NAME,
                            PROP_FIRMWARE_REVISION,PROP_APP_SOFTWARE_REV,
                            PROP_PROTOCOL_VERSION,PROP_PROTOCOL_REVISION,
                            PROP_MAX_APDU_LENGTH,PROP_SEGMENTATION,
                            PROP_APDU_TIMEOUT,PROP_NUM_APDU_RETRIES,
                            PROP_DATABASE_REVISION,PROP_OBJECT_LIST };
                        for (int x=0; x<(int)(sizeof(dp)/sizeof(dp[0])); x++)
                            props[nprops++]=dp[x];
                    } else if (fobj) {
                        int is_bin=(fobj->type==OBJECT_BINARY_INPUT||
                                    fobj->type==OBJECT_BINARY_OUTPUT||
                                    fobj->type==OBJECT_BINARY_VALUE);
                        int is_cmd=(fobj->type==OBJECT_ANALOG_OUTPUT||
                                    fobj->type==OBJECT_BINARY_OUTPUT);
                        props[nprops++]=PROP_OBJECT_IDENTIFIER;
                        props[nprops++]=PROP_OBJECT_NAME;
                        props[nprops++]=PROP_OBJECT_TYPE;
                        props[nprops++]=PROP_DESCRIPTION;
                        props[nprops++]=PROP_PRESENT_VALUE;
                        props[nprops++]=PROP_STATUS_FLAGS;
                        props[nprops++]=PROP_EVENT_STATE;
                        props[nprops++]=PROP_OUT_OF_SERVICE;
                        props[nprops++]=PROP_RELIABILITY;
                        if (is_bin) {
                            props[nprops++]=PROP_POLARITY;
                            /* ACTIVE_TEXT=4, INACTIVE_TEXT=46 — added last
                             * so any encoding collision is at packet tail */
                            props[nprops++]=PROP_ACTIVE_TEXT;
                            props[nprops++]=PROP_INACTIVE_TEXT;
                        } else {
                            props[nprops++]=PROP_UNITS;
                        }
                        if (is_cmd) {
                            props[nprops++]=PROP_PRIORITY_ARRAY;
                            props[nprops++]=PROP_RELINQUISH_DEFAULT;
                        }
                    }
                }

                if (!fdev||nprops==0) continue;

                rlen2+=encode_context_object_id(resp+rlen2,0,rtype,rinst);
                resp[rlen2++]=0x1E;

                for (int pi=0; pi<nprops; pi++) {
                    uint32_t pid=props[pi];
                    /* propertyIdentifier [2] — use proper context encoding */
                    rlen2 += encode_context_unsigned(resp+rlen2, 2, pid);
                    uint8_t tmp[512];
                    int vl=encode_property_value(tmp,fdev,fobj,fis_dev,pid,-1);
                    if (vl>0) {
                        resp[rlen2++]=0x4E;
                        memcpy(resp+rlen2,tmp,(size_t)vl);
                        rlen2+=vl;
                        resp[rlen2++]=0x4F;
                    } else {
                        resp[rlen2++]=0x5E;
                        resp[rlen2++]=0x09; resp[rlen2++]=2;
                        resp[rlen2++]=0x19; resp[rlen2++]=32;
                        resp[rlen2++]=0x5F;
                    }
                    if (rlen2>1350) break;
                }
                resp[rlen2++]=0x1F;
                if (rlen2>1400) break;
            }

            fix_bvlc_length(resp,rlen2);
            sendto(sock,(char*)resp,rlen2,0,
                   (struct sockaddr*)from,sizeof(*from));
        }

        /* ── WriteProperty ─────────────────────────────────────── */
        else if (svc == SERVICE_CONFIRMED_WRITE_PROP) {
            /*
             * Wire format:
             *   [0] objectIdentifier   context-OID  (tag 0x0C, 4 bytes)
             *   [1] propertyIdentifier context-uint
             *   [2] propertyArrayIndex (optional)
             *   [3] propertyValue  opening 0x3E ... closing 0x3F
             *   [4] priority (optional)
             */
            int wp = data_off;

            /* [0] Object identifier tag + 4 bytes */
            if (wp + 5 > pkt_len) goto wp_err;
            wp++; /* skip tag byte */
            uint32_t wp_oid  = decode_u32(pkt+wp); wp+=4;
            uint16_t wp_type = (uint16_t)(wp_oid>>22);
            uint32_t wp_inst = wp_oid & 0x3FFFFF;

            /* [1] Property identifier */
            if (wp >= pkt_len) goto wp_err;
            { uint8_t pt=pkt[wp++]; uint8_t pl=pt&0x07;
              uint32_t wp_prop=0;
              for (uint8_t pi=0;pi<pl&&pi<4;pi++) wp_prop=(wp_prop<<8)|pkt[wp++];

            /* [2] Skip optional array index */
            if (wp<pkt_len && (pkt[wp]&0xF8)==0x28) {
                uint8_t al=pkt[wp++]&0x07; if(al==5)al=pkt[wp++]; wp+=al; }

            /* [3] propertyValue opening tag 0x3E */
            if (wp>=pkt_len || pkt[wp]!=0x3E) goto wp_err;
            wp++;

            /* Decode application-tagged value */
            if (wp>=pkt_len) goto wp_err;
            uint8_t vtag=pkt[wp++];
            uint8_t atype=(vtag>>4)&0x0F;
            uint8_t vlen=vtag&0x07;
            float   new_f=0.0f; uint32_t new_u=0;
            int got_f=0, got_u=0;

            if (atype==APP_TAG_REAL && vlen==4 && wp+4<=pkt_len) {
                union{uint32_t u;float f;}fu; fu.u=decode_u32(pkt+wp);
                new_f=fu.f; got_f=1; wp+=4;
            } else if ((atype==APP_TAG_ENUM||atype==APP_TAG_UNSIGNED)&&vlen<=4) {
                for(uint8_t b=0;b<vlen;b++) new_u=(new_u<<8)|pkt[wp++];
                got_u=1;
            } else if (atype==APP_TAG_BOOL) {
                new_u=vlen; got_u=1; /* bool value is in len field */
            }

            /* Find the object */
            BacnetDevice *wp_dev=NULL; BacnetObject *wp_obj=NULL;
            if (wp_type==OBJECT_DEVICE) {
                for(int d=0;d<NUM_DEVICES;d++)
                    if(g_devices[d].device_id==wp_inst){wp_dev=&g_devices[d];break;}
            } else {
                wp_obj=find_object(wp_type,wp_inst,&wp_dev);
            }

            if (wp_obj) {
                bool is_bin=(wp_obj->type==OBJECT_BINARY_INPUT||
                             wp_obj->type==OBJECT_BINARY_OUTPUT||
                             wp_obj->type==OBJECT_BINARY_VALUE);
                if (wp_prop==PROP_PRESENT_VALUE) {
                    if (is_bin && got_u) {
                        wp_obj->binary_value=(new_u!=0);
                        printf("[WP] Binary PV type=%u inst=%u -> %u\n",
                               wp_type,wp_inst,new_u);
                    } else if (!is_bin && got_f) {
                        wp_obj->present_value=new_f;
                        printf("[WP] Analog PV type=%u inst=%u -> %.3f\n",
                               wp_type,wp_inst,new_f);
                    }
                    /* Trigger COV for all subscribers of this object */
                    for(int s=0;s<g_num_subs;s++){
                        CovSub *sub=&g_subs[s];
                        if(!sub->active||sub->obj_type!=wp_type||
                           sub->obj_inst!=wp_inst) continue;
                        uint8_t notif[512];
                        int nl=build_cov_notification(notif,sub,wp_dev,wp_obj);
                        sendto(sock,(char*)notif,nl,0,
                               (struct sockaddr*)&sub->addr,sizeof(sub->addr));
                        sub->last_analog=wp_obj->present_value;
                        sub->last_binary=(int)wp_obj->binary_value;
                    }
                } else if (wp_prop==PROP_OUT_OF_SERVICE && got_u) {
                    wp_obj->out_of_service=(new_u!=0);
                    printf("[WP] OutOfService type=%u inst=%u -> %u\n",
                           wp_type,wp_inst,new_u);
                } else {
                    printf("[WP] prop=%u not writable (ACK anyway)\n",wp_prop);
                }
            } else {
                printf("[WP] Object not found type=%u inst=%u\n",wp_type,wp_inst);
            }

            rlen=build_simple_ack(resp,invoke_id,SERVICE_CONFIRMED_WRITE_PROP);
            sendto(sock,(char*)resp,rlen,0,(struct sockaddr*)from,sizeof(*from));
            goto wp_done;
            (void)pt; }

        wp_err:
            printf("[WP] Parse error\n");
            rlen=build_error(resp,invoke_id,SERVICE_CONFIRMED_WRITE_PROP,2,32);
            sendto(sock,(char*)resp,rlen,0,(struct sockaddr*)from,sizeof(*from));
        wp_done:;
        }
    }
}

/* -------------------------------------------------------═══════
 *  Main loop
 * -------------------------------------------------------═══════ */
int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    memset(g_subs, 0, sizeof(g_subs));
    init_devices();

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) { perror("socket"); return 1; }

    int bcast=1, reuse=1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(BACNET_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind – try: sudo ./bacnet_sim");
        closesocket(sock);
        return 1;
    }

    printf("-------------------------------------------------------\n");
    printf("  BACnet/IP Simulator v2  -  port %d\n", BACNET_PORT);
    printf("  Devices:\n");
    for (int d=0; d<NUM_DEVICES; d++)
        printf("    [%d] ID=%-6u  %s\n", d, g_devices[d].device_id, g_devices[d].name);
    printf("-------------------------------------------------------\n");
    printf("  YABE: Add UDP => port 47808 => click (+) WhoIs\n");
    printf("  All 3 devices will appear.\n");
    printf("  Right-click any object value => Subscribe (COV) for live updates.\n");
    printf("-------------------------------------------------------\n\n");

    /* Non-blocking / timeout so we can run tick and COV loop */
#ifdef _WIN32
    u_long nb=1;
    ioctlsocket(sock, FIONBIO, &nb);
#else
    struct timeval tv = { .tv_sec=0, .tv_usec=100000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    uint8_t buf[1500];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    unsigned tick_ctr = 0;
    unsigned cov_ctr  = 0;

    for (;;) {
        int n = recvfrom(sock, (char*)buf, sizeof(buf), 0,
                         (struct sockaddr*)&from, &fromlen);
        if (n > 0)
            dispatch(sock, buf, n, &from);

        /* Update simulated values every ~1 second (10 × 100ms) */
        tick_ctr++;
        if (tick_ctr >= 10) {
            tick_ctr = 0;
            tick_values();
        }

        /* Check and send COV notifications every ~500ms */
        cov_ctr++;
        if (cov_ctr >= 5) {
            cov_ctr = 0;
            send_cov_notifications(sock);
        }

#ifdef _WIN32
        Sleep(100);
#endif
    }

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}