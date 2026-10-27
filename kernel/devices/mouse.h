#define MOUSE_BUF_SIZE 128

#define MOUSE_BUTTON_LEFT   (1 << 0)
#define MOUSE_BUTTON_RIGHT  (1 << 1)
#define MOUSE_BUTTON_MIDDLE (1 << 2)

typedef struct
{
    int16_t dx;
    int16_t dy;
    int8_t  wheel;
    uint8_t buttons;
    uint16_t reserved;
}
MouseEvent;

void mouse_init(void);
void mouse_push_event(const MouseEvent *ev);
size_t mouse_read_raw(void *buf, size_t count);
bool mouse_has_data(void);

void ps2_mouse_init(void);
void ps2_mouse_feed(uint8_t data);
