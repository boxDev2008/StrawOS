#define KBD_BUF_SIZE 256

typedef struct
{
    uint8_t scancode;
}
KbdEvent;

void keyboard_init(void);
void keyboard_push_scancode(uint8_t sc);
size_t keyboard_read_raw(void *buf, size_t count);
bool keyboard_has_data(void);
