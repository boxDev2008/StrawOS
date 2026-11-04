VNode *fat32_mount(BlockDevice *dev);
int    fat32_unmount(VNode *root);
bool   fat32_is_root(VNode *node);
bool   fat32_owns(VNode *root, VNode *node);
bool   fat32_has_label(BlockDevice *dev, const char *label);
int    fat32_format(BlockDevice *dev, const char *label);
