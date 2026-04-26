#ifndef BACNET_H
#define BACNET_H

#include <stdint.h>
#include <stdbool.h>

/* ── BACnet UDP port ─────────────────────────────────────────── */
#define BACNET_PORT          47808   /* 0xBAC0 */
#define BACNET_MAX_APDU      1476
#define BACNET_BVLC_HDR      4       /* Type(1) Func(1) Len(2)   */

/* ── BVLC function codes ─────────────────────────────────────── */
#define BVLC_TYPE            0x81
#define BVLC_ORIGINAL_UNICAST_NPDU   0x0A
#define BVLC_ORIGINAL_BROADCAST_NPDU 0x0B

/* ── NPDU control flags ──────────────────────────────────────── */
#define NPDU_VERSION         0x01
#define NPDU_NO_APDU_MSG     0x00
#define NPDU_EXPECTING_REPLY 0x04

/* ── APDU types ──────────────────────────────────────────────── */
#define PDU_TYPE_CONFIRMED   0x00
#define PDU_TYPE_UNCONFIRMED 0x10
#define PDU_TYPE_SIMPLE_ACK  0x20
#define PDU_TYPE_COMPLEX_ACK 0x30
#define PDU_TYPE_ERROR       0x50

/* ── Unconfirmed services ────────────────────────────────────── */
#define SERVICE_UNCONFIRMED_WHO_IS       8
#define SERVICE_UNCONFIRMED_I_AM         0
#define SERVICE_UNCONFIRMED_COV_NOTIF    2

/* ── Confirmed services ──────────────────────────────────────── */
#define SERVICE_CONFIRMED_READ_PROP      12
#define SERVICE_CONFIRMED_WRITE_PROP     15
#define SERVICE_CONFIRMED_READ_PROP_MULT 14

/* ── BACnet object types ─────────────────────────────────────── */
#define OBJECT_DEVICE        8
#define OBJECT_ANALOG_INPUT  0
#define OBJECT_ANALOG_OUTPUT 1
#define OBJECT_ANALOG_VALUE  2
#define OBJECT_BINARY_INPUT  3
#define OBJECT_BINARY_OUTPUT 4
#define OBJECT_BINARY_VALUE  5

/* ── BACnet property IDs ─────────────────────────────────────── */
#define PROP_OBJECT_IDENTIFIER   75
#define PROP_OBJECT_NAME         77
#define PROP_OBJECT_TYPE         79
#define PROP_PRESENT_VALUE       85
#define PROP_DESCRIPTION         28
#define PROP_STATUS_FLAGS        111
#define PROP_EVENT_STATE         36
#define PROP_OUT_OF_SERVICE      81
#define PROP_UNITS               117
#define PROP_SYSTEM_STATUS       112
#define PROP_VENDOR_NAME         121
#define PROP_VENDOR_IDENTIFIER   120
#define PROP_MODEL_NAME          70
#define PROP_FIRMWARE_REVISION   44
#define PROP_APP_SOFTWARE_REV    12
#define PROP_PROTOCOL_VERSION    98
#define PROP_PROTOCOL_REVISION   139
#define PROP_PROTOCOL_SERVICES   97
#define PROP_PROTOCOL_OBJ_TYPES  96
#define PROP_OBJECT_LIST         76
#define PROP_MAX_APDU_LENGTH     62
#define PROP_SEGMENTATION        107
#define PROP_APDU_TIMEOUT        11
#define PROP_NUM_APDU_RETRIES    73
#define PROP_DEVICE_ADDRESS_BIND 30
#define PROP_DATABASE_REVISION   155
#define PROP_POLARITY            84
#define PROP_RELIABILITY         103
#define PROP_ACTIVE_TEXT         4
#define PROP_INACTIVE_TEXT       46
#define PROP_RELINQUISH_DEFAULT  104
#define PROP_PRIORITY_ARRAY      87
#define PROP_RESOLUTION          106
#define PROP_COV_INCREMENT       22
#define PROP_MIN_PRES_VALUE      69
#define PROP_MAX_PRES_VALUE      65

/* ── COV subscription ───────────────────────────────────────── */
#define MAX_COV_SUBS    32

typedef struct {
    bool     active;
    uint32_t process_id;       /* subscriber process id      */
    uint32_t device_id;        /* which BACnet device owns   */
    uint16_t obj_type;
    uint32_t obj_instance;
    uint32_t lifetime;         /* seconds, 0=permanent       */
    uint32_t expires;          /* wall-clock expiry tick     */
    bool     issue_confirmed;
    uint8_t  subscriber_addr[4];
    uint16_t subscriber_port;
    float    cov_increment;    /* analog change threshold    */
    float    last_value;       /* last notified value        */
    bool     last_binary;      /* last notified binary state */
} CovSubscription;

/* ── Engineering units (subset) ─────────────────────────────── */
#define UNITS_DEGREES_CELSIUS    62
#define UNITS_PERCENT            98
#define UNITS_PASCALS            53
#define UNITS_WATTS              47
#define UNITS_NO_UNITS           95
#define UNITS_VOLTS              5
#define UNITS_AMPERES            1
#define UNITS_CUBIC_METERS       28

/* ── Application tags ────────────────────────────────────────── */
#define APP_TAG_NULL             0
#define APP_TAG_BOOL             1
#define APP_TAG_UNSIGNED         2
#define APP_TAG_SIGNED           3
#define APP_TAG_REAL             4
#define APP_TAG_DOUBLE           5
#define APP_TAG_OCTET_STRING     6
#define APP_TAG_CHARACTER_STRING 7
#define APP_TAG_BIT_STRING       8
#define APP_TAG_ENUM             9
#define APP_TAG_OBJECT_ID        12

/* ── Device/object structures ───────────────────────────────── */
typedef struct {
    uint32_t instance;       /* object instance number      */
    uint16_t type;           /* OBJECT_* constant           */
    char     name[64];
    char     description[64];
    float    present_value;
    uint16_t units;
    bool     out_of_service;
    bool     binary_value;   /* used for binary objects     */
    bool     polarity;       /* normal=false, reverse=true  */
} BacnetObject;

#define MAX_OBJECTS 32

typedef struct {
    uint32_t    device_id;
    char        name[64];
    char        vendor_name[64];
    char        model_name[64];
    char        description[64];
    uint16_t    vendor_id;
    uint32_t    db_revision;
    BacnetObject objects[MAX_OBJECTS];
    int         obj_count;
} BacnetDevice;

#endif /* BACNET_H */