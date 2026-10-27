#define SX_SETTINGS_FILE_MAX 4096

static size_t sx_settings_read(char *buf, size_t size)
{
    int fd = open(SX_SETTINGS_PATH, O_RDONLY);
    if (fd < 0)
    {
        buf[0] = '\0';
        return 0;
    }

    size_t len = 0;
    for (;;)
    {
        ssize_t n = read(fd, buf + len, size - 1 - len);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len == size - 1)
            break;
    }
    close(fd);
    buf[len] = '\0';
    return len;
}

static const char *sx_settings_find(const char *buf, const char *key, size_t *value_len)
{
    size_t key_len = strlen(key);
    const char *line = buf;
    while (*line)
    {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > key_len && strncmp(line, key, key_len) == 0 && line[key_len] == '=')
        {
            *value_len = len - key_len - 1;
            return line + key_len + 1;
        }
        if (!end)
            break;
        line = end + 1;
    }
    return NULL;
}

bool sx_setting_get(const char *key, char *out, size_t size)
{
    char buf[SX_SETTINGS_FILE_MAX];
    sx_settings_read(buf, sizeof(buf));

    size_t len;
    const char *value = sx_settings_find(buf, key, &len);
    if (!value)
        return false;
    while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == ' '))
        len--;
    if (len >= size)
        len = size - 1;
    memcpy(out, value, len);
    out[len] = '\0';
    return true;
}

int sx_setting_get_int(const char *key, int fallback)
{
    char buf[32];
    if (!sx_setting_get(key, buf, sizeof(buf)) || !buf[0])
        return fallback;
    return atoi(buf);
}

bool sx_setting_set(const char *key, const char *value)
{
    char buf[SX_SETTINGS_FILE_MAX];
    char out[SX_SETTINGS_FILE_MAX];
    sx_settings_read(buf, sizeof(buf));

    size_t key_len = strlen(key);
    size_t pos = 0;
    bool written = false;

    const char *line = buf;
    while (*line)
    {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        bool match = len > key_len && strncmp(line, key, key_len) == 0 && line[key_len] == '=';

        if (match)
        {
            int n = snprintf(out + pos, sizeof(out) - pos, "%s=%s\n", key, value);
            if (n < 0 || (size_t)n >= sizeof(out) - pos)
                return false;
            pos += (size_t)n;
            written = true;
        }
        else if (len > 0)
        {
            if (pos + len + 1 >= sizeof(out))
                return false;
            memcpy(out + pos, line, len);
            pos += len;
            out[pos++] = '\n';
        }

        if (!end)
            break;
        line = end + 1;
    }

    if (!written)
    {
        int n = snprintf(out + pos, sizeof(out) - pos, "%s=%s\n", key, value);
        if (n < 0 || (size_t)n >= sizeof(out) - pos)
            return false;
        pos += (size_t)n;
    }

    struct stat st;
    if (stat("/config", &st) != 0)
        mkdir("/config", 0);
    unlink(SX_SETTINGS_PATH);
    int fd = open(SX_SETTINGS_PATH, O_WRONLY | O_CREAT);
    if (fd < 0)
        return false;
    bool ok = write(fd, out, pos) == (ssize_t)pos;
    close(fd);
    return ok;
}

bool sx_setting_set_int(const char *key, int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return sx_setting_set(key, buf);
}

void sx_settings_apply(void)
{
    if (!sx_conn.connected && sx_connect() != 0)
        return;
    SxMsgHeader msg;
    sx_send(&msg, SX_REQ_RELOAD_SETTINGS, sizeof(msg), 0);
}
