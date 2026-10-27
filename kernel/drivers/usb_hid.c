static const uint16_t usb_hid_usage_to_set1[0x66] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
    [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
    [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
    [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14,
    [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,

    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
    [0x26] = 0x0A, [0x27] = 0x0B,

    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A,
    [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27,
    [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34,
    [0x38] = 0x35, [0x39] = 0x3A,

    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E,
    [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
    [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,

    [0x46] = 0xE037, [0x47] = 0x46,
    [0x49] = 0xE052, [0x4A] = 0xE047, [0x4B] = 0xE049,
    [0x4C] = 0xE053, [0x4D] = 0xE04F, [0x4E] = 0xE051,
    [0x4F] = 0xE04D, [0x50] = 0xE04B, [0x51] = 0xE050, [0x52] = 0xE048,

    [0x53] = 0x45,   [0x54] = 0xE035, [0x55] = 0x37, [0x56] = 0x4A,
    [0x57] = 0x4E,   [0x58] = 0xE01C,
    [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B,
    [0x5D] = 0x4C, [0x5E] = 0x4D, [0x5F] = 0x47, [0x60] = 0x48,
    [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53,
    [0x64] = 0x56, [0x65] = 0xE05D,
};

static const uint16_t usb_hid_modifier_to_set1[8] = {
    0x1D, 0x2A, 0x38, 0xE05B, 0xE01D, 0x36, 0xE038, 0xE05C
};

static void usb_hid_emit(uint16_t code, int pressed)
{
    if (code == 0)
        return;

    if ((code >> 8) == 0xE0)
        keyboard_push_scancode(0xE0);

    keyboard_push_scancode((uint8_t)(code & 0x7F) | (pressed ? 0 : 0x80));
}

static void usb_hid_emit_usage(uint8_t usage, int pressed)
{
    if (usage < ARRAY_LEN(usb_hid_usage_to_set1))
        usb_hid_emit(usb_hid_usage_to_set1[usage], pressed);
}

static int usb_hid_report_has_key(const uint8_t *report, uint8_t usage)
{
    for (int i = 2; i < 8; i++)
        if (report[i] == usage)
            return 1;
    return 0;
}

void usb_hid_keyboard_report(UsbHidKeyboard *kbd, const uint8_t *report, size_t len)
{
    if (len < 8)
        return;

    if (report[2] == 0x01)
        return;

    uint8_t mods_changed = report[0] ^ kbd->prev[0];
    for (int bit = 0; bit < 8; bit++)
        if (mods_changed & (1 << bit))
            usb_hid_emit(usb_hid_modifier_to_set1[bit], report[0] & (1 << bit));

    for (int i = 2; i < 8; i++)
    {
        uint8_t usage = kbd->prev[i];
        if (usage >= 0x04 && !usb_hid_report_has_key(report, usage))
        {
            usb_hid_emit_usage(usage, 0);
            if (usage == kbd->repeat_usage)
                kbd->repeat_usage = 0;
        }
    }

    for (int i = 2; i < 8; i++)
    {
        uint8_t usage = report[i];
        if (usage >= 0x04 && !usb_hid_report_has_key(kbd->prev, usage))
        {
            usb_hid_emit_usage(usage, 1);
            kbd->repeat_usage = usage;
            kbd->repeat_ticks = 0;
        }
    }

    memcpy(kbd->prev, report, 8);
}

void usb_hid_keyboard_tick(UsbHidKeyboard *kbd)
{
    if (!kbd->repeat_usage)
        return;

    kbd->repeat_ticks++;
    if (kbd->repeat_ticks >= USB_HID_REPEAT_DELAY_TICKS &&
        (kbd->repeat_ticks - USB_HID_REPEAT_DELAY_TICKS) % USB_HID_REPEAT_RATE_TICKS == 0)
        usb_hid_emit_usage(kbd->repeat_usage, 1);
}

void usb_hid_mouse_report(const uint8_t *report, size_t len)
{
    if (len < 3)
        return;

    MouseEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.buttons = report[0] & 0x07;
    ev.dx      = (int8_t)report[1];
    ev.dy      = (int8_t)report[2];
    ev.wheel   = len >= 4 ? (int8_t)report[3] : 0;

    mouse_push_event(&ev);
}

#define HID_ITEM_MAIN   0
#define HID_ITEM_GLOBAL 1
#define HID_ITEM_LOCAL  2

#define HID_MAIN_INPUT          0x8
#define HID_MAIN_COLLECTION     0xA
#define HID_MAIN_END_COLLECTION 0xC

#define HID_GLOBAL_USAGE_PAGE   0x0
#define HID_GLOBAL_LOGICAL_MIN  0x1
#define HID_GLOBAL_REPORT_SIZE  0x7
#define HID_GLOBAL_REPORT_ID    0x8
#define HID_GLOBAL_REPORT_COUNT 0x9

#define HID_LOCAL_USAGE         0x0
#define HID_LOCAL_USAGE_MIN     0x1
#define HID_LOCAL_USAGE_MAX     0x2

#define HID_INPUT_CONSTANT      (1 << 0)
#define HID_INPUT_VARIABLE      (1 << 1)
#define HID_INPUT_RELATIVE      (1 << 2)

#define HID_PAGE_GENERIC_DESKTOP 0x01
#define HID_PAGE_BUTTON          0x09
#define HID_USAGE_MOUSE          0x02
#define HID_USAGE_X              0x30
#define HID_USAGE_Y              0x31
#define HID_USAGE_WHEEL          0x38

#define HID_MAX_USAGES 16

#define HID_USAGE(page, id) (((uint32_t)(page) << 16) | (id))

int usb_hid_parse_mouse_layout(const uint8_t *desc, size_t len, UsbHidMouseLayout *out)
{
    memset(out, 0, sizeof(*out));

    uint32_t usage_page = 0, report_size = 0, report_count = 0;
    int32_t  logical_min = 0;
    uint8_t  report_id = 0;
    int      uses_report_ids = 0;

    uint32_t usages[HID_MAX_USAGES];
    int      num_usages = 0;
    uint32_t usage_min = 0, usage_max = 0;
    int      have_range = 0;

    int depth = 0, mouse_depth = -1;
    int locked = 0;

    static uint16_t bit_offsets[256];
    memset(bit_offsets, 0, sizeof(bit_offsets));

    for (size_t i = 0; i < len; )
    {
        uint8_t prefix = desc[i++];
        if (prefix == 0xFE)
        {
            if (i + 1 >= len)
                break;
            i += 2 + desc[i];
            continue;
        }

        uint32_t size = prefix & 0x3;
        if (size == 3)
            size = 4;
        uint32_t type = (prefix >> 2) & 0x3;
        uint32_t tag  = prefix >> 4;
        if (i + size > len)
            break;

        uint32_t data = 0;
        for (uint32_t b = 0; b < size; b++)
            data |= (uint32_t)desc[i + b] << (8 * b);
        int32_t sdata = (int32_t)data;
        if (size == 1)
            sdata = (int8_t)data;
        else if (size == 2)
            sdata = (int16_t)data;
        i += size;

        if (type == HID_ITEM_GLOBAL)
        {
            switch (tag)
            {
                case HID_GLOBAL_USAGE_PAGE:   usage_page = data; break;
                case HID_GLOBAL_LOGICAL_MIN:  logical_min = sdata; break;
                case HID_GLOBAL_REPORT_SIZE:  report_size = data; break;
                case HID_GLOBAL_REPORT_COUNT: report_count = data; break;
                case HID_GLOBAL_REPORT_ID:
                    report_id = (uint8_t)data;
                    uses_report_ids = 1;
                    break;
            }
        }
        else if (type == HID_ITEM_LOCAL)
        {
            uint32_t usage = size == 4 ? data : HID_USAGE(usage_page, data);
            switch (tag)
            {
                case HID_LOCAL_USAGE:
                    if (num_usages < HID_MAX_USAGES)
                        usages[num_usages++] = usage;
                    break;
                case HID_LOCAL_USAGE_MIN: usage_min = usage; have_range = 1; break;
                case HID_LOCAL_USAGE_MAX: usage_max = usage; have_range = 1; break;
            }
        }
        else if (type == HID_ITEM_MAIN)
        {
            if (tag == HID_MAIN_COLLECTION)
            {
                depth++;
                if (data == 1 && mouse_depth < 0 && num_usages > 0 &&
                    usages[0] == HID_USAGE(HID_PAGE_GENERIC_DESKTOP, HID_USAGE_MOUSE))
                    mouse_depth = depth;
            }
            else if (tag == HID_MAIN_END_COLLECTION)
            {
                if (depth == mouse_depth)
                    mouse_depth = -1;
                if (depth > 0)
                    depth--;
            }
            else if (tag == HID_MAIN_INPUT)
            {
                uint16_t base = bit_offsets[report_id];
                bit_offsets[report_id] = (uint16_t)(base + report_size * report_count);

                int want = mouse_depth >= 0 && (data & HID_INPUT_VARIABLE) &&
                           !(data & HID_INPUT_CONSTANT) &&
                           (!locked || report_id == out->report_id) &&
                           report_size > 0 && report_size <= 32;

                for (uint32_t f = 0; want && f < report_count; f++)
                {
                    uint32_t usage;
                    if (have_range)
                        usage = usage_min + f;
                    else if (num_usages > 0)
                        usage = usages[(int)f < num_usages ? (int)f : num_usages - 1];
                    else
                        break;

                    int relative = (data & HID_INPUT_RELATIVE) != 0;
                    UsbHidField field = {
                        (uint16_t)(base + f * report_size), (uint8_t)report_size, logical_min < 0,
                    };

                    if (usage == HID_USAGE(HID_PAGE_GENERIC_DESKTOP, HID_USAGE_X) && relative && !out->x.size)
                    {
                        out->x = field;
                        out->report_id = uses_report_ids ? report_id : 0;
                        locked = 1;
                    }
                    else if (usage == HID_USAGE(HID_PAGE_GENERIC_DESKTOP, HID_USAGE_Y) && relative && !out->y.size)
                        out->y = field;
                    else if (usage == HID_USAGE(HID_PAGE_GENERIC_DESKTOP, HID_USAGE_WHEEL) && relative && !out->wheel.size)
                        out->wheel = field;
                    else if ((usage >> 16) == HID_PAGE_BUTTON && (usage & 0xFFFF) == 1 &&
                             report_size == 1 && !out->buttons_count)
                    {
                        out->buttons_offset = field.offset;
                        out->buttons_count = (uint8_t)MIN(report_count - f, 3);
                    }
                }
            }

            num_usages = 0;
            have_range = 0;
            usage_min = usage_max = 0;
        }
    }

    return out->x.size && out->y.size;
}

static int32_t usb_hid_read_field(const UsbHidField *field, const uint8_t *data, size_t len)
{
    if (!field->size || (size_t)(field->offset + field->size + 7) / 8 > len)
        return 0;

    uint32_t value = 0;
    for (uint32_t b = 0; b < field->size; b++)
    {
        uint32_t bit = field->offset + b;
        if (data[bit / 8] & (1u << (bit % 8)))
            value |= 1u << b;
    }

    if (field->is_signed && field->size < 32 && (value & (1u << (field->size - 1))))
        value |= ~0u << field->size;
    return (int32_t)value;
}

void usb_hid_mouse_report_layout(const UsbHidMouseLayout *layout, const uint8_t *report, size_t len)
{
    if (layout->report_id)
    {
        if (len < 1 || report[0] != layout->report_id)
            return;
        report++;
        len--;
    }

    int32_t dx = usb_hid_read_field(&layout->x, report, len);
    int32_t dy = usb_hid_read_field(&layout->y, report, len);

    MouseEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.dx    = (int16_t)MAX(-32768, MIN(dx, 32767));
    ev.dy    = (int16_t)MAX(-32768, MIN(dy, 32767));
    ev.wheel = (int8_t)MAX(-128, MIN(usb_hid_read_field(&layout->wheel, report, len), 127));

    for (uint8_t b = 0; b < layout->buttons_count; b++)
    {
        UsbHidField bit = { (uint16_t)(layout->buttons_offset + b), 1, 0 };
        if (usb_hid_read_field(&bit, report, len))
            ev.buttons |= (uint8_t)(1 << b);
    }

    mouse_push_event(&ev);
}
