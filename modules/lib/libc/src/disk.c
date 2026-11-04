int mount(const char *device, const char *path)
{
    char dev[PATH_BUF], full[PATH_BUF];
    if (!abs_path(device, dev) || !abs_path(path, full))
        return -1;
    return (int)kcall(KCALL_MOUNT, (uint64_t)dev, (uint64_t)full, 0, 0);
}

int umount(const char *path)
{
    char full[PATH_BUF];
    if (!abs_path(path, full))
        return -1;
    return (int)kcall(KCALL_UNMOUNT, (uint64_t)full, 0, 0, 0);
}
