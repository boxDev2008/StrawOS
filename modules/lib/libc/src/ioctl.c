int ioctl(int fd, uint64_t request, void *arg)
{
    return (int)kcall(KCALL_IOCTL, (uint64_t)fd, request, (uint64_t)arg, 0);
}