#define USB_HID_PROTO_KEYBOARD 1
#define USB_HID_PROTO_MOUSE    2

#define USB_HID_REPEAT_DELAY_TICKS (SCHEDULER_HZ / 2)
#define USB_HID_REPEAT_RATE_TICKS  (SCHEDULER_HZ / 30)

typedef struct
{
    uint8_t  prev[8];
    uint8_t  repeat_usage;
    uint32_t repeat_ticks;
}
UsbHidKeyboard;

typedef struct
{
    uint16_t offset;
    uint8_t  size;
    uint8_t  is_signed;
}
UsbHidField;

typedef struct
{
    uint8_t     report_id;
    UsbHidField x, y, wheel;
    uint16_t    buttons_offset;
    uint8_t     buttons_count;
}
UsbHidMouseLayout;

void usb_hid_keyboard_report(UsbHidKeyboard *kbd, const uint8_t *report, size_t len);
void usb_hid_keyboard_tick(UsbHidKeyboard *kbd);
void usb_hid_mouse_report(const uint8_t *report, size_t len);

int  usb_hid_parse_mouse_layout(const uint8_t *desc, size_t len, UsbHidMouseLayout *out);
void usb_hid_mouse_report_layout(const UsbHidMouseLayout *layout, const uint8_t *report, size_t len);
