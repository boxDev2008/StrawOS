static volatile MouseEvent mouse_ring[MOUSE_BUF_SIZE];
static volatile size_t mouse_head = 0, mouse_tail = 0;

void mouse_push_event(const MouseEvent *ev)
{
    size_t next = (mouse_head + 1) % MOUSE_BUF_SIZE;
    if (next != mouse_tail)
    {
        mouse_ring[mouse_head] = *ev;
        mouse_head = next;
    }
    task_wake_waiters();
}

bool mouse_has_data(void)
{
    return mouse_tail != mouse_head;
}

size_t mouse_read_raw(void *buf, size_t count)
{
    MouseEvent *out = (MouseEvent *)buf;
    size_t max = count / sizeof(MouseEvent);
    size_t n = 0;

    while (n < max && mouse_tail != mouse_head)
    {
        out[n++] = mouse_ring[mouse_tail];
        mouse_tail = (mouse_tail + 1) % MOUSE_BUF_SIZE;
    }
    return n * sizeof(MouseEvent);
}

void mouse_init(void)
{
    mouse_head = mouse_tail = 0;
}
