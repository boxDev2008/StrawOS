#include "init.h"
#include "devices/lfb.h"
#include "devices/rtc.h"
#include "graphics/graphics.h"
#define NK_ASSERT
#include "graphics/ui.h"

#include "sys/task.h"

#include <printk.h>
#include <string.h>

#include "graphics/nuklear.h"
#include "graphics/images/logo.h"

#include "graphics/images/wallpaper.h"

#include "devices/ide.h"
#include "fs/fatfs/ff.h"

#include "terminal.h"
#include "mm/memmap.h"

#include "devices/ps2/keyboard.h"
#include "devices/ps2/mouse.h"

uint32_t *wallpaper_current_ptr = wallpaper_waves_01;

kernel_memory_map_t kmap;
void __stack_chk_fail(void){}
void __attribute__ ((noreturn))
__stack_chk_fail_local (void){__stack_chk_fail ();}

void kmain(unsigned long magic, unsigned long addr)
{
    multiboot_info_t *mboot_info = (multiboot_info_t*)addr;
    terminal_initialize();

    kernel_initialize(mboot_info);

    keyboard_initialize();
    //mouse_initialize();

    ata_initialize();
    ide_print_drive_info();

    FATFS fs;
    BYTE work[FF_MAX_SS];

    //printk("%i\n", f_mkfs("", FM_ANY, 0, work, sizeof(work)));

    printk("%i\n", f_mount(&fs, "", FM_FAT));

    tasking_initialize();

    printk("Switching to otherTask... \n");
    task_yield();
    printk("Returned to mainTask!\n");

    f_unmount("");

    /*graphics_initialize(1440, 900, 32);

    ui_initialize();

    date_time_t dt = { 0 };
    char dt1_str[8];
    char dt2_str[16];

    while (true)
    {
        ui_begin();
        rtc_get_time(&dt);
        sprintf(dt1_str, "%i:%i:%i", dt.hour, dt.min, dt.sec);
        sprintf(dt2_str, "%i/%i/%i", dt.month, dt.day, dt.year);
        nk_begin(nk_ctx, "__Taskbar__", nk_rect(0.0f, 0.0f, lfb_width, 38.0f), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER);
        nk_layout_row_static(nk_ctx, 32.0f, 64.0f, 3);
        nk_button_label(nk_ctx, "Menu");
        nk_label(nk_ctx, dt1_str, NK_TEXT_RIGHT);
        nk_label(nk_ctx, dt2_str, NK_TEXT_RIGHT);
        nk_end(nk_ctx);
        
        {
            uint32_t *wallpaper = wallpaper_current_ptr;
            uint32_t *back_buffer = graphics_back_buffer;

            uint32_t *end_buffer = wallpaper + lfb_width * lfb_height;

            for (; wallpaper < end_buffer; wallpaper++, back_buffer++)
                *back_buffer = *wallpaper;
        }
        ui_end();
    }

    ui_shutdown();

    graphics_shutdown();*/
}