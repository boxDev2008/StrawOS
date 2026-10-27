static void sx_copy_trimmed(char *dst, size_t size, const char *src, size_t len)
{
    while (len > 0 && (*src == ' ' || *src == '\t'))
    {
        src++;
        len--;
    }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r'))
        len--;
    if (len >= size)
        len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

bool sx_desktop_entry_load(const char *path, SxDesktopEntry *out)
{
    memset(out, 0, sizeof(*out));

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    char buf[2048];
    size_t len = 0;
    for (;;)
    {
        ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0)
            break;
        len += (size_t)n;
        if (len == sizeof(buf) - 1)
            break;
    }
    close(fd);
    buf[len] = '\0';

    bool in_entry = false;
    char *line = buf;
    while (line && *line)
    {
        char *next = strchr(line, '\n');
        size_t line_len = next ? (size_t)(next - line) : strlen(line);

        char trimmed[300];
        sx_copy_trimmed(trimmed, sizeof(trimmed), line, line_len);

        if (trimmed[0] == '[')
        {
            in_entry = strcmp(trimmed, "[Desktop Entry]") == 0;
        }
        else if (in_entry && trimmed[0] && trimmed[0] != '#')
        {
            char *eq = strchr(trimmed, '=');
            if (eq)
            {
                *eq = '\0';
                char key[32];
                sx_copy_trimmed(key, sizeof(key), trimmed, strlen(trimmed));
                const char *value = eq + 1;
                size_t vlen = strlen(value);

                if (strcmp(key, "Name") == 0)
                    sx_copy_trimmed(out->name, sizeof(out->name), value, vlen);
                else if (strcmp(key, "Comment") == 0)
                    sx_copy_trimmed(out->comment, sizeof(out->comment), value, vlen);
                else if (strcmp(key, "Icon") == 0)
                    sx_copy_trimmed(out->icon, sizeof(out->icon), value, vlen);
                else if (strcmp(key, "Exec") == 0)
                    sx_copy_trimmed(out->exec, sizeof(out->exec), value, vlen);
            }
        }

        line = next ? next + 1 : NULL;
    }

    return out->name[0] && out->exec[0];
}

bool sx_desktop_entry_launch(const SxDesktopEntry *entry)
{
    char cmd[sizeof(entry->exec)];
    strcpy(cmd, entry->exec);

    const char *argv[16];
    int argc = 0;
    char *p = cmd;
    while (*p && argc < 15)
    {
        while (*p == ' ')
            *p++ = '\0';
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
    }
    argv[argc] = NULL;

    if (argc == 0)
        return false;
    return spawn(argv[0], argv) != 0;
}

bool sx_is_desktop_file(const char *name)
{
    size_t len = strlen(name);
    return len > 8 && strcmp(name + len - 8, ".desktop") == 0;
}
