#ifndef ENCODE_H
#define ENCODE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ── Primitive encoders ─────────────────────────────────────── */

static inline int encode_tag(uint8_t *buf, uint8_t tag_num,
                              int context, uint32_t len_val)
{
    int len = 0;
    uint8_t tag = 0;

    if (context)
        tag |= 0x08;
    if (tag_num <= 14)
        tag |= (uint8_t)(tag_num << 4);
    else {
        tag |= 0xF0;
    }

    if (len_val <= 4)
        tag |= (uint8_t)len_val;
    else
        tag |= 5;

    buf[len++] = tag;

    if (tag_num > 14)
        buf[len++] = tag_num;

    if (len_val > 4) {
        if (len_val <= 253) {
            buf[len++] = (uint8_t)len_val;
        } else {
            buf[len++] = 254;
            buf[len++] = (uint8_t)(len_val >> 8);
            buf[len++] = (uint8_t)(len_val);
        }
    }
    return len;
}

/* Opening / closing context tags */
static inline int encode_opening_tag(uint8_t *buf, uint8_t tag_num)
{
    if (tag_num <= 14) {
        buf[0] = (uint8_t)((tag_num << 4) | 0x0E);
        return 1;
    } else {
        buf[0] = 0xFE; /* extended tag, opening */
        buf[1] = tag_num;
        return 2;
    }
}
static inline int encode_closing_tag(uint8_t *buf, uint8_t tag_num)
{
    if (tag_num <= 14) {
        buf[0] = (uint8_t)((tag_num << 4) | 0x0F);
        return 1;
    } else {
        buf[0] = 0xFF; /* extended tag, closing */
        buf[1] = tag_num;
        return 2;
    }
}

/* Unsigned integer */
static inline int encode_app_unsigned(uint8_t *buf, uint32_t val)
{
    int len = 0;
    if (val < 0x100) {
        len += encode_tag(buf + len, APP_TAG_UNSIGNED, 0, 1);
        buf[len++] = (uint8_t)val;
    } else if (val < 0x10000) {
        len += encode_tag(buf + len, APP_TAG_UNSIGNED, 0, 2);
        buf[len++] = (uint8_t)(val >> 8);
        buf[len++] = (uint8_t)val;
    } else {
        len += encode_tag(buf + len, APP_TAG_UNSIGNED, 0, 4);
        buf[len++] = (uint8_t)(val >> 24);
        buf[len++] = (uint8_t)(val >> 16);
        buf[len++] = (uint8_t)(val >> 8);
        buf[len++] = (uint8_t)val;
    }
    return len;
}

/* Enumerated */
static inline int encode_app_enumerated(uint8_t *buf, uint32_t val)
{
    int len = 0;
    len += encode_tag(buf + len, APP_TAG_ENUM, 0, 1);
    buf[len++] = (uint8_t)val;
    return len;
}

/* Real (float) */
static inline int encode_app_real(uint8_t *buf, float val)
{
    int len = 0;
    union { float f; uint32_t u; } fu;
    fu.f = val;
    len += encode_tag(buf + len, APP_TAG_REAL, 0, 4);
    buf[len++] = (uint8_t)(fu.u >> 24);
    buf[len++] = (uint8_t)(fu.u >> 16);
    buf[len++] = (uint8_t)(fu.u >> 8);
    buf[len++] = (uint8_t)fu.u;
    return len;
}

/* Boolean */
static inline int encode_app_boolean(uint8_t *buf, int val)
{
    buf[0] = (uint8_t)((APP_TAG_BOOL << 4) | (val ? 1 : 0));
    return 1;
}

/* Object identifier  (type << 22) | instance */
static inline int encode_app_object_id(uint8_t *buf,
                                        uint16_t type, uint32_t inst)
{
    uint32_t v = ((uint32_t)type << 22) | (inst & 0x3FFFFF);
    int len = 0;
    len += encode_tag(buf + len, APP_TAG_OBJECT_ID, 0, 4);
    buf[len++] = (uint8_t)(v >> 24);
    buf[len++] = (uint8_t)(v >> 16);
    buf[len++] = (uint8_t)(v >> 8);
    buf[len++] = (uint8_t)v;
    return len;
}

/* Context-tagged object identifier */
static inline int encode_context_object_id(uint8_t *buf, uint8_t ctx,
                                            uint16_t type, uint32_t inst)
{
    uint32_t v = ((uint32_t)type << 22) | (inst & 0x3FFFFF);
    int len = 0;
    len += encode_tag(buf + len, ctx, 1, 4);
    buf[len++] = (uint8_t)(v >> 24);
    buf[len++] = (uint8_t)(v >> 16);
    buf[len++] = (uint8_t)(v >> 8);
    buf[len++] = (uint8_t)v;
    return len;
}

/* Context-tagged unsigned */
static inline int encode_context_unsigned(uint8_t *buf, uint8_t ctx,
                                           uint32_t val)
{
    int len = 0;
    if (val < 0x100) {
        len += encode_tag(buf + len, ctx, 1, 1);
        buf[len++] = (uint8_t)val;
    } else if (val < 0x10000) {
        len += encode_tag(buf + len, ctx, 1, 2);
        buf[len++] = (uint8_t)(val >> 8);
        buf[len++] = (uint8_t)val;
    } else {
        len += encode_tag(buf + len, ctx, 1, 4);
        buf[len++] = (uint8_t)(val >> 24);
        buf[len++] = (uint8_t)(val >> 16);
        buf[len++] = (uint8_t)(val >> 8);
        buf[len++] = (uint8_t)val;
    }
    return len;
}

/* Character string (UTF-8 / ANSI) */
static inline int encode_app_character_string(uint8_t *buf, const char *str)
{
    int slen = (int)strlen(str);
    int len  = 0;
    len += encode_tag(buf + len, APP_TAG_CHARACTER_STRING, 0,
                      (uint32_t)(slen + 1));
    buf[len++] = 0x00; /* encoding: UTF-8 */
    memcpy(buf + len, str, (size_t)slen);
    len += slen;
    return len;
}

/* Bit-string: statusFlags is always 4 bits */
static inline int encode_status_flags(uint8_t *buf,
                                       int in_alarm, int fault,
                                       int overridden, int oos)
{
    int len = 0;
    len += encode_tag(buf + len, APP_TAG_BIT_STRING, 0, 2);
    buf[len++] = 4; /* unused bits */
    buf[len++] = (uint8_t)(
        (in_alarm   ? 0x80 : 0) |
        (fault      ? 0x40 : 0) |
        (overridden ? 0x20 : 0) |
        (oos        ? 0x10 : 0));
    return len;
}

/* ── Decoders ────────────────────────────────────────────────── */

static inline uint16_t decode_u16(const uint8_t *buf)
{
    return (uint16_t)((buf[0] << 8) | buf[1]);
}
static inline uint32_t decode_u32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

#endif /* ENCODE_H */