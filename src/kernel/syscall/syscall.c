#include "syscall.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/boot_diag.h"
#include "../../drivers/serial/serial.h"
#include "../../drivers/display/gop.h"
#include "../../drivers/display/vga.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/mouse/usb_mouse.h"
#include "../../drivers/storage/block_device.h"
#include "../../drivers/storage/ahci.h"
#include "../../drivers/storage/storage_probe.h"
#include "../../drivers/usb/xhci.h"
#include "../../drivers/usb/ehci.h"
#include "../../drivers/interrupts/timer.h"
#include "../../drivers/audio/audio.h"
#include "../../fs/vfs.h"
#include "../core/system_info.h"
#include "../../lib/string.h"
#include "../../drivers/power/power.h"
#include "../../drivers/input/keyboard.h"
#include "../process/scheduler.h"
#include "../process/process.h"
#include "../../mm/pmm.h"
#include "../../userspace/userspace.h"
#include "../../userspace/window_manager.h"
#include "../../net/api/ping.h"
#include "../../net/wifi/wifi.h"
#include <stdint.h>
#include <stdbool.h>

#define INSTALL_WORKER_PRIORITY 3

static volatile bool filesystem_syscall_busy;
static struct install_status install_job;
static struct install_log install_history;
static char install_device[STORAGE_DEVICE_NAME_CAPACITY];
static char install_serial[STORAGE_SERIAL_CAPACITY];
static uint8_t install_fs_type;

static bool readable(const void *buffer, uint64_t size){
    return process_user_buffer(buffer,size,false);
}

static bool writable(void *buffer, uint64_t size){
    return process_user_buffer(buffer,size,true);
}

static bool readable_string(const char *text){
    return process_user_string(text,4096);
}

void filesystem_syscall_lock(void){
    while(__atomic_test_and_set(&filesystem_syscall_busy,__ATOMIC_ACQUIRE))
        scheduler_yield();
}

void filesystem_syscall_unlock(void){
    __atomic_clear(&filesystem_syscall_busy,__ATOMIC_RELEASE);
}

static void install_progress(uint32_t progress, const char *stage){
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    install_job.progress=progress;
    if(stage) strncpy(install_job.stage,stage,sizeof(install_job.stage)-1);
    install_job.stage[sizeof(install_job.stage)-1]='\0';
    if(stage){
        if(install_history.count==INSTALL_LOG_CAPACITY){
            for(uint32_t index=1;index<INSTALL_LOG_CAPACITY;index++)
                install_history.entries[index-1]=install_history.entries[index];
            install_history.count--;
        }
        struct install_log_entry *entry=
            &install_history.entries[install_history.count++];
        entry->progress=progress;
        strncpy(entry->stage,stage,sizeof(entry->stage)-1);
        entry->stage[sizeof(entry->stage)-1]='\0';
    }
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}

static void install_worker(void *argument){
    (void)argument;
    filesystem_syscall_lock();
    block_device_begin_exclusive_io();
    int32_t result;
    result=fat32_format_uefi_device_progress_ex(install_device,install_serial,install_progress,install_fs_type);
    block_device_end_exclusive_io();
    filesystem_syscall_unlock();
    install_job.result=result;
    if(result<0){
        install_job.state=3;
        char failed_stage[INSTALL_STAGE_CAPACITY];
        const char *prefix="Failed: ";
        uint32_t length=0;
        while(prefix[length] && length+1<sizeof(failed_stage)){
            failed_stage[length]=prefix[length];
            length++;
        }
        for(uint32_t index=0;install_job.stage[index]
            && length+1<sizeof(failed_stage);index++)
            failed_stage[length++]=install_job.stage[index];
        failed_stage[length]='\0';
        install_progress(install_job.progress,failed_stage);
    } else {
        install_job.state=2;
        install_progress(92,"Disk layout complete");
    }
}

void install_report_ui_crash(int32_t status){
    if(install_job.state!=0) return;
    memset(&install_job,0,sizeof(install_job));
    memset(&install_history,0,sizeof(install_history));
    install_job.state=3;
    install_job.result=status;
    install_progress(0,"Installer UI crashed");
}

static void print_hex(uint64_t v){
    const char *h="0123456789ABCDEF";
    char buf[17]; buf[16]=0;
    for(int i=15;i>=0;i--){ buf[i]=h[v&0xF]; v>>=4; }
    serial_write_string("0x"); serial_write_string(buf);
}

int64_t syscall_handler(struct syscall_regs *r){
    uint64_t n = r->rax;
    uint64_t a1 = r->rbx;
    uint64_t a2 = r->rcx;
    uint64_t a3 = r->rdx;
    switch(n){
        case SYS_WRITE: {
            const char *s = (const char*)(uintptr_t)a1;
            uint64_t len = a2;
            if(!readable(s,len)) return -1;
            for(uint64_t i=0;i<len;i++){
                serial_putc(s[i]);
                if(gop_console_is_active()) gop_console_putc(s[i]);
                else gop_putc(s[i]);
            }
            return (int64_t)len;
        }
        case SYS_CLEAR: {
            uint32_t color = (uint32_t)a1;
            gop_clear(color);
            if(!gop_is_available()) vga_clear();
            return 0;
        }
        case SYS_DRAW_RECT: {
            uint32_t x=(uint32_t)a1, y=(uint32_t)a2, w=(uint32_t)a3, h=(uint32_t)r->rsi;
            uint32_t c=(uint32_t)r->rdi;
            gop_draw_rect(x,y,w,h,c);
            return 0;
        }
        case SYS_DRAW_LINE: {
            uint32_t x0=(uint32_t)a1, y0=(uint32_t)a2, x1=(uint32_t)a3, y1=(uint32_t)r->rsi;
            uint32_t c=(uint32_t)r->rdi;
            gop_draw_line(x0,y0,x1,y1,c);
            return 0;
        }
        case SYS_FB_INFO: {
            struct framebuffer_info *info=(struct framebuffer_info*)(uintptr_t)a1;
            if(!writable(info,sizeof(*info))) return -1;
            memset(info,0,sizeof(*info));
            info->width=gop_get_width();
            info->height=gop_get_height();
            info->pitch=gop_get_pitch();
            info->size_bytes=gop_get_framebuffer_size_bytes();
            info->bpp=gop_get_bpp();
            info->available=gop_is_available() ? 1 : 0;
            strncpy(info->protocol_name,gop_get_protocol_name(),
                    sizeof(info->protocol_name)-1);
            return 0;
        }
        case SYS_DRAW_TEXT: {
            struct framebuffer_text_request *request=
                (struct framebuffer_text_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))
               || !readable_string(request->text)) return -1;
            gop_draw_text_at(request->x,request->y,request->text,
                             request->fg,request->bg);
            return 0;
        }
        case SYS_DRAW_TEXT_SIZED: {
            struct framebuffer_text_request *request=
                (struct framebuffer_text_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))
               || !readable_string(request->text)) return -1;
            gop_draw_text_sized_at(request->x,request->y,request->text,
                                   request->fg,request->bg,request->size);
            return 0;
        }
        case SYS_SCROLL_RECT_UP: {
            struct framebuffer_scroll_request *request=
                (struct framebuffer_scroll_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))) return -1;
            gop_scroll_rect_up(request->x,request->y,request->w,request->h,
                               request->amount,request->fill_color);
            return 0;
        }
        case SYS_SET_FONT_FACE:
            if(a1>GOP_FONT_BOLD) return -1;
            gop_set_font_face((enum gop_font_face)a1);
            return 0;
        case SYS_GET_FONT_FACE:
            return (int64_t)gop_get_font_face();
        case SYS_FB_BEGIN_UPDATE:
            mouse_begin_framebuffer_update();
            return 0;
        case SYS_FB_END_UPDATE:
            mouse_end_framebuffer_update();
            return 0;
        case SYS_CONSOLE_CONFIGURE: {
            const struct framebuffer_console_request *request=
                (const struct framebuffer_console_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))) return -1;
            return gop_console_configure(
                request->x,request->y,request->width,request->height,
                request->foreground,request->background) ? 0 : -1;
        }
        case SYS_CONSOLE_CLEAR:
            if(!gop_console_is_active()) return -1;
            gop_console_clear();
            return 0;
        case SYS_CONSOLE_DISABLE:
            gop_console_disable();
            return 0;
        case SYS_DESKTOP_REDRAW:
            userspace_redraw_desktop();
            return 0;
        case SYS_GUI_WINDOW_REGISTER: {
            const struct gui_window_request *request=
                (const struct gui_window_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))) return -1;
            return window_manager_register((uint32_t)process_current_pid(),
                                            request) ? 0 : -1;
        }
        case SYS_GUI_WINDOW_UPDATE: {
            const struct gui_window_request *request=
                (const struct gui_window_request*)(uintptr_t)a1;
            if(!readable(request,sizeof(*request))) return -1;
            return window_manager_update((uint32_t)process_current_pid(),
                                          request) ? 0 : -1;
        }
        case SYS_GUI_WINDOW_UNREGISTER:
            window_manager_unregister((uint32_t)process_current_pid());
            return 0;
        case SYS_GUI_WINDOW_STATE:
            return window_manager_state((uint32_t)process_current_pid());
        case SYS_GUI_WINDOW_REPAINT_DONE:
            window_manager_finish_repaint((uint32_t)process_current_pid());
            return 0;
        case SYS_GETPID:
            return process_current_pid();
        case SYS_HEAP_GROW: {
            uint64_t address=process_heap_grow(a1);
            return address ? (int64_t)address : -1;
        }
        case SYS_EXEC:
            if(!readable_string((const char*)(uintptr_t)a1)
               || (a2 && !readable_string((const char*)(uintptr_t)a2)))
                return -1;
            if(a2 && strlen((const char*)(uintptr_t)a2)
                     >=PROCESS_COMMAND_LINE_CAPACITY) return -1;
            return process_spawn_module((const char*)(uintptr_t)a1,
                                        (const char*)(uintptr_t)a2);
        case SYS_GET_COMMAND_LINE:
            if(a2>PROCESS_COMMAND_LINE_CAPACITY) return -1;
            if(!writable((void*)(uintptr_t)a1,(uint32_t)a2)) return -1;
            return process_command_line((char*)(uintptr_t)a1,(uint32_t)a2);
        case SYS_GET_PROCESS_NAME:
            if(a2>32 || !writable((void*)(uintptr_t)a1,(uint32_t)a2)) return -1;
            return process_name((char*)(uintptr_t)a1,(uint32_t)a2);
        case SYS_ENV_GET:
            if(a3>PROCESS_ENVIRONMENT_VALUE_CAPACITY) return -1;
            if(!readable_string((const char*)(uintptr_t)a1)
               || !writable((void*)(uintptr_t)a2,(uint32_t)a3)) return -1;
            return process_environment_get((const char*)(uintptr_t)a1,
                                           (char*)(uintptr_t)a2,(uint32_t)a3);
        case SYS_ENV_SET:
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -1;
            return process_environment_set((const char*)(uintptr_t)a1,
                                           (const char*)(uintptr_t)a2);
        case SYS_ENV_UNSET:
            if(!readable_string((const char*)(uintptr_t)a1)) return -1;
            return process_environment_unset((const char*)(uintptr_t)a1);
        case SYS_ENV_LIST: {
            if(a2>PROCESS_ENVIRONMENT_LIMIT) return -1;
            struct process_environment_variable *variables=
                (struct process_environment_variable*)(uintptr_t)a1;
            if(a2 && !writable(variables,a2*sizeof(*variables))) return -1;
            struct process_environment_entry entries[PROCESS_ENVIRONMENT_LIMIT];
            int32_t count=process_environment_list(entries,(uint32_t)a2);
            if(count<0) return count;
            uint32_t copied=(uint32_t)count<(uint32_t)a2
                ? (uint32_t)count : (uint32_t)a2;
            for(uint32_t index=0;index<copied;index++){
                variables[index].used=entries[index].used ? 1 : 0;
                strncpy(variables[index].name,entries[index].name,
                        sizeof(variables[index].name)-1);
                variables[index].name[sizeof(variables[index].name)-1]='\0';
                strncpy(variables[index].value,entries[index].value,
                        sizeof(variables[index].value)-1);
                variables[index].value[sizeof(variables[index].value)-1]='\0';
            }
            return count;
        }
        case SYS_GET_MOUSE: {
            struct mouse_state *out = (struct mouse_state*)(uintptr_t)a1;
            if(!writable(out,sizeof(*out))) return -1;
            ps2_mouse_poll();
            usb_mouse_poll();
            *out = mouse_get_state();
            return 0;
        }
        case SYS_MOUSE_DEBUG_GET:
            return mouse_get_debug_overlay() ? 1 : 0;
        case SYS_MOUSE_DEBUG_SET:
            mouse_set_debug_overlay(a1!=0);
            return 0;
        case SYS_FILE_OPEN: {
            if(!readable_string((const char*)(uintptr_t)a1)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_open((const char*)(uintptr_t)a1);
            if(result>=0){
                int32_t descriptor=process_fd_install(result);
                if(descriptor<0) (void)vfs_close(result);
                result=descriptor;
            }
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_READ: {
            if(!writable((void*)(uintptr_t)a2,(uint32_t)a3)) return -1;
            int32_t descriptor=process_fd_resolve((int32_t)a1);
            if(descriptor<0) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_read(descriptor,(void*)(uintptr_t)a2,(uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_SEEK: {
            int32_t descriptor=process_fd_resolve((int32_t)a1);
            if(descriptor<0) return -1;
            filesystem_syscall_lock();
            int64_t result=vfs_seek(descriptor,(int64_t)a2,(uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_STAT: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !writable((void*)(uintptr_t)a2,sizeof(struct file_stat_info)))
                return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_stat((const char*)(uintptr_t)a1,
                                    (struct file_stat_info*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_CLOSE: {
            filesystem_syscall_lock();
            int32_t result=process_fd_close((int32_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_DELETE: {
            if(!readable_string((const char*)(uintptr_t)a1)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_delete((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_RENAME: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_rename((const char*)(uintptr_t)a1,
                                      (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_MOVE: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_move((const char*)(uintptr_t)a1,
                                    (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DIR_LIST: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !writable((void*)(uintptr_t)a2,
                            (uint64_t)(uint32_t)a3
                                *sizeof(struct fs_directory_entry))) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_list((const char*)(uintptr_t)a1,
                                    (struct fs_directory_entry*)(uintptr_t)a2,
                                    (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DIR_LIST_LONG: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !writable((void*)(uintptr_t)a2,
                            (uint64_t)(uint32_t)a3
                                *sizeof(struct fs_directory_entry_long))) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_list_long((const char*)(uintptr_t)a1,
                                    (struct fs_directory_entry_long*)(uintptr_t)a2,
                                    (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_CREATE: {
            if(!readable_string((const char*)(uintptr_t)a1)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_create_file((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DIR_CREATE: {
            if(!readable_string((const char*)(uintptr_t)a1)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_create_directory((const char*)(uintptr_t)a1);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_DISK_LIST:
            if(!writable((void*)(uintptr_t)a1,
                         (uint64_t)(uint32_t)a2
                             *sizeof(struct storage_device_info))) return -1;
            return block_device_list((struct storage_device_info*)(uintptr_t)a1,
                                     (uint32_t)a2);
        case SYS_STORAGE_CONTROLLERS:
            if(!writable((void*)(uintptr_t)a1,
                         (uint64_t)(uint32_t)a2
                             *sizeof(struct storage_controller_info))) return -1;
            return storage_controller_list(
                (struct storage_controller_info*)(uintptr_t)a1,(uint32_t)a2);
        case SYS_FAT32_FORMAT: {
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -1;
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)
               || (a3 && !readable_string((const char*)(uintptr_t)a3))) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_format_device((const char*)(uintptr_t)a1,
                                             (const char*)(uintptr_t)a2,
                                             (const char*)(uintptr_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FAT32_FORMAT_FORCE: {
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -1;
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_format_device_force((const char*)(uintptr_t)a1,
                                                   (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FAT32_FORMAT_UEFI: {
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -1;
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_format_uefi_device((const char*)(uintptr_t)a1,
                                                  (const char*)(uintptr_t)a2);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_WRITE: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable((const void*)(uintptr_t)a2,(uint32_t)a3)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_write_file((const char*)(uintptr_t)a1,
                                          (const void*)(uintptr_t)a2,
                                          (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_FILE_APPEND: {
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable((const void*)(uintptr_t)a2,(uint32_t)a3)) return -1;
            filesystem_syscall_lock();
            int32_t result=vfs_append_file((const char*)(uintptr_t)a1,
                                           (const void*)(uintptr_t)a2,
                                           (uint32_t)a3);
            filesystem_syscall_unlock();
            return result;
        }
        case SYS_USB_RESCAN: {
            uint32_t count=block_device_rescan_usb();
            struct xhci_probe_stats xhci_stats;
            struct ehci_probe_stats ehci_stats;
            xhci_get_probe_stats(&xhci_stats);
            ehci_get_probe_stats(&ehci_stats);
            struct usb_scan_status *status=(struct usb_scan_status*)(uintptr_t)a1;
            if(status && !writable(status,sizeof(*status))) return -1;
            if(status){
                status->xhci_controllers=xhci_stats.controllers;
                status->xhci_connected_ports=xhci_stats.connected_ports;
                status->xhci_addressed_devices=xhci_stats.addressed_devices;
                status->xhci_disks=xhci_stats.mass_storage_devices;
                status->xhci_hid_mice=xhci_stats.hid_mice;
                status->xhci_hid_interfaces=xhci_stats.hid_interfaces;
                status->xhci_hubs=xhci_stats.hubs;
                status->xhci_mouse_transfer_errors=xhci_stats.mouse_transfer_errors;
                status->xhci_failures=xhci_stats.failures;
                status->xhci_stage=xhci_stats.last_stage;
                status->xhci_error=xhci_stats.last_error;
                status->xhci_last_port=xhci_stats.last_port;
                status->xhci_portsc=xhci_stats.last_portsc;
                status->xhci_completion_code=xhci_stats.last_completion_code;
                status->xhci_max_ports=xhci_stats.max_ports;
                status->xhci_usb_status=xhci_stats.usb_status;
                status->xhci_scratchpad_count=xhci_stats.scratchpad_count;
                status->ehci_connected_ports=ehci_stats.connected_ports;
                status->ehci_high_speed_ports=ehci_stats.high_speed_ports;
                status->ehci_disks=ehci_stats.mass_storage_devices;
                status->ehci_failures=ehci_stats.failures;
                status->ehci_stage=ehci_stats.last_stage;
            }
            klogf(KLOG_INFO,"usb-rescan: xhci connected=%u addressed=%u disks=%u failures=%u stage=%u",
                  xhci_stats.connected_ports,xhci_stats.addressed_devices,
                  xhci_stats.mass_storage_devices,xhci_stats.failures,
                  xhci_stats.last_stage);
            klogf(KLOG_INFO,"usb-rescan: xhci error=%u scratchpads=%u port=%u portsc=0x%x completion=%u usbsts=0x%x",
                  xhci_stats.last_error,xhci_stats.scratchpad_count,
                  xhci_stats.last_port,xhci_stats.last_portsc,
                  xhci_stats.last_completion_code,xhci_stats.usb_status);
            klogf(KLOG_INFO,"usb-rescan: ehci connected=%u high-speed=%u disks=%u failures=%u stage=%u",
                  ehci_stats.connected_ports,ehci_stats.high_speed_ports,
                  ehci_stats.mass_storage_devices,ehci_stats.failures,
                  ehci_stats.last_stage);
            if(count){
                filesystem_syscall_lock();
                if(!vfs_is_root_mounted()) (void)vfs_mount_root();
                filesystem_syscall_unlock();
            }
            return count;
        }
        case SYS_CPU_INFO: {
            struct cpu_monitor_info *info=(struct cpu_monitor_info*)(uintptr_t)a1;
            if(!writable(info,sizeof(*info))) return -1;
            memset(info,0,sizeof(*info));
            strncpy(info->name,system_info_cpu_name(),sizeof(info->name)-1);
            info->logical_processors=system_info_logical_processors();
            info->usage_percent=system_info_cpu_usage_percent();
            info->frequency_hz=system_info_tsc_frequency_hz();
            info->uptime_ms=system_info_uptime_ms();
            return 0;
        }
        case SYS_MEMORY_INFO: {
            struct memory_monitor_info *info=(struct memory_monitor_info*)(uintptr_t)a1;
            if(!writable(info,sizeof(*info))) return -1;
            info->total_bytes=system_info_total_ram_bytes();
            info->available_bytes=pmm_free_bytes();
            info->used_bytes=info->total_bytes>info->available_bytes
                ? info->total_bytes-info->available_bytes : 0;
            info->framebuffer_bytes=gop_get_framebuffer_size_bytes();
            return 0;
        }
        case SYS_PROCESS_LIST: {
            if(a2>PROCESS_MAX_COUNT) return -1;
            struct process_monitor_info *entries=
                (struct process_monitor_info*)(uintptr_t)a1;
            if(a2 && !writable(entries,a2*sizeof(*entries))) return -1;
            return process_monitor_list(entries,(uint32_t)a2);
        }
        case SYS_DISK_STATS: {
            struct disk_monitor_info *info=(struct disk_monitor_info*)(uintptr_t)a1;
            if(!writable(info,sizeof(*info))) return -1;
            memset(info,0,sizeof(*info));
            info->device_count=block_device_count();
            for(uint32_t index=0;index<info->device_count;index++){
                struct storage_device_info device;
                if(!block_device_get_info(index,&device)) continue;
                if(device.operational) info->operational_count++;
                info->total_bytes+=device.sector_count*device.sector_size;
            }
            return 0;
        }
        case SYS_EXIT:
            process_exit_current((int32_t)a1);
        case SYS_WAIT:
            if(a2 && !writable((void*)(uintptr_t)a2,sizeof(int32_t))) return -1;
            return process_wait((uint32_t)a1,(int32_t*)(uintptr_t)a2,a3!=0);
        case SYS_SLEEP:
            if(a1>UINT32_MAX) return -1;
            scheduler_sleep((uint32_t)a1);
            return 0;
        case SYS_REBOOT:
            power_reboot();
            return 0;
        case SYS_SHUTDOWN:
            power_shutdown();
            return 0;
        case SYS_BATTERY_INFO: {
            struct battery_info *out=(struct battery_info*)(uintptr_t)a1;
            if(!writable(out,sizeof(*out))) return -1;
            return power_battery_get(out) ? 0 : -1;
        }
        case SYS_AUDIO_GET_STATUS: {
            struct audio_status *out=(struct audio_status*)(uintptr_t)a1;
            if(!writable(out,sizeof(*out))) return -1;
            audio_get_status(out);
            return 0;
        }
        case SYS_AUDIO_GET_VOLUME:
            audio_update();
            return audio_get_volume();
        case SYS_AUDIO_SET_VOLUME:
            if(a1>100) return -1;
            audio_set_volume((uint8_t)a1);
            return 0;
        case SYS_AUDIO_IS_MUTED:
            audio_update();
            return audio_is_muted() ? 1 : 0;
        case SYS_AUDIO_SET_MUTED:
            audio_set_muted(a1!=0);
            return 0;
        case SYS_AUDIO_ADJUST_VOLUME:
            audio_adjust_volume((int8_t)(int64_t)a1);
            return 0;
        case SYS_AUDIO_PLAY_TEST_SOUND:
            audio_play_test_sound();
            return 0;
        case SYS_AUDIO_PLAY_TONE: {
            uint32_t freq=(uint32_t)a1;
            uint32_t dur=(uint32_t)a2;
            if(freq<30 || freq>8000 || dur==0 || dur>5000) return -1;
            audio_play_tone(freq,dur);
            return 0;
        }
        case SYS_AUDIO_STOP_TONE:
            audio_stop_tone();
            return 0;
        case SYS_AUDIO_PCM_PUSH: {
            uint32_t frames=(uint32_t)a2;
            if(frames==0){
                audio_pcm_eos();
                return 0;
            }
            const int16_t *samples=(const int16_t*)(uintptr_t)a1;
            if(frames>4096) return -1;
            if(!readable(samples,(uint64_t)frames*2U)) return -1;
            return audio_pcm_push(samples,frames);
        }
        case SYS_AUDIO_PCM_START:
            return audio_pcm_start() ? 0 : -1;
        case SYS_AUDIO_PCM_STOP:
            audio_pcm_stop_stream();
            return 0;
        case SYS_AUDIO_UPDATE:
            audio_update();
            return 0;
        case SYS_AUDIO_SELECT_OUTPUT_DEVICE:
            klogf(KLOG_INFO, "audio: syscall select output device index=%u",
                  (uint32_t)a1);
            return audio_select_output_device((uint32_t)a1) ? 0 : -1;
        case SYS_SCHED_YIELD:
            scheduler_yield();
            return 0;
        case SYS_GETCHAR:
            return (uint8_t)keyboard_getc();
        case SYS_TRY_GETCHAR: {
            char character;
            return keyboard_try_getc(&character) ? (uint8_t)character : -1;
        }
        case SYS_TRY_GET_SPECIAL: {
            uint8_t key;
            return keyboard_try_get_special(&key) ? key : -1;
        }
        case SYS_INSTALL_START:
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -10;
            if(!readable_string((const char*)(uintptr_t)a1)
               || !readable_string((const char*)(uintptr_t)a2)) return -11;
            if(install_job.state==1) return -12;
            memset(&install_job,0,sizeof(install_job));
            memset(&install_history,0,sizeof(install_history));
            strncpy(install_device,(const char*)(uintptr_t)a1,
                    sizeof(install_device)-1);
            strncpy(install_serial,(const char*)(uintptr_t)a2,
                    sizeof(install_serial)-1);
            install_fs_type = FS_TYPE_FAT32;
            install_job.state=1;
            install_progress(1,"Starting installer worker");
            if(scheduler_create_thread(install_worker,0,"installer-io",
                                       INSTALL_WORKER_PRIORITY,-1)<0){
                install_job.state=3;
                install_job.result=-1;
                install_progress(100,"Cannot start installer worker");
                return -1;
            }
            return 0;
        case SYS_INSTALL_START_EX: {
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -10;
            const struct install_start_request *req=(const struct install_start_request*)(uintptr_t)a1;
            if(!readable(req,sizeof(*req))) return -11;
            if(install_job.state==1) return -12;
            memset(&install_job,0,sizeof(install_job));
            memset(&install_history,0,sizeof(install_history));
            strncpy(install_device,req->device,sizeof(install_device)-1);
            strncpy(install_serial,req->serial,sizeof(install_serial)-1);
            install_fs_type = req->fs_type <= FS_TYPE_EXT2 ? req->fs_type : FS_TYPE_FAT32;
            install_job.state=1;
            install_progress(1,"Starting installer worker");
            if(scheduler_create_thread(install_worker,0,"installer-io",INSTALL_WORKER_PRIORITY,-1)<0){
                install_job.state=3; install_job.result=-1;
                install_progress(100,"Cannot start installer worker");
                return -1;
            }
            return 0;
        }
        case SYS_INSTALL_STATUS:
            if(!writable((void*)(uintptr_t)a1,sizeof(install_job))) return -1;
            *(struct install_status*)(uintptr_t)a1=install_job;
            return 0;
        case SYS_INSTALL_LOG:
            if(!writable((void*)(uintptr_t)a1,sizeof(install_history))) return -1;
            *(struct install_log*)(uintptr_t)a1=install_history;
            return 0;
        case SYS_NET_PING: {
            const struct network_ping_request *user_request=
                (const struct network_ping_request*)(uintptr_t)a1;
            struct network_ping_result *user_result=
                (struct network_ping_result*)(uintptr_t)a2;
            if(!readable(user_request,sizeof(*user_request))
               || !writable(user_result,sizeof(*user_result))) return -1;
            struct network_ping_request request=*user_request;
            request.target[sizeof(request.target)-1]='\0';
            bool terminated=false;
            for(uint32_t index=0;index<sizeof(request.target);index++){
                if(!request.target[index]){ terminated=true; break; }
            }
            if(!terminated || !request.target[0] || request.timeout_ms>30000)
                return NET_PING_INVALID;
            struct net_ping_reply reply;
            enum net_ping_status status=net_ping_target(
                request.target,request.sequence,request.timeout_ms,&reply);
            if(status!=NET_PING_OK) return status;
            user_result->address=reply.address;
            user_result->round_trip_ms=reply.round_trip_ms;
            user_result->sequence=reply.sequence;
            user_result->ttl=reply.ttl;
            user_result->reserved=0;
            return NET_PING_OK;
        }
        case SYS_WIFI_SCAN: {
            if(!wifi_has_device()) return -1;
            return wifi_trigger_scan() ? 0 : -1;
        }
        case SYS_WIFI_LIST: {
            struct wifi_network_info *buffer=(struct wifi_network_info*)(uintptr_t)a1;
            uint32_t capacity=(uint32_t)a2;
            if(capacity>WIFI_SCAN_MAX) return -1;
            if(capacity && !writable(buffer,capacity*sizeof(*buffer))) return -1;
            if(!capacity) return (int64_t)wifi_get_scan_results(0,0);
            struct wifi_network temp[WIFI_SCAN_MAX];
            uint32_t count=wifi_get_scan_results((struct wifi_network*)temp,WIFI_SCAN_MAX);
            if(count>capacity) count=capacity;
            for(uint32_t i=0;i<count;i++){
                buffer[i]=*(struct wifi_network_info*)&temp[i];
            }
            return (int64_t)count;
        }
        case SYS_WIFI_CONNECT: {
            const struct wifi_connect_request *user_req=(const struct wifi_connect_request*)(uintptr_t)a1;
            if(!readable(user_req,sizeof(*user_req))) return -1;
            struct wifi_connect_request req=*user_req;
            req.ssid[sizeof(req.ssid)-1]='\0';
            req.password[sizeof(req.password)-1]='\0';
            bool ssid_term=false;
            for(uint32_t i=0;i<sizeof(req.ssid);i++) if(!req.ssid[i]){ssid_term=true;break;}
            if(!ssid_term || !req.ssid[0]) return -1;
            bool pass_term=false;
            for(uint32_t i=0;i<sizeof(req.password);i++) if(!req.password[i]){pass_term=true;break;}
            if(!pass_term) return -1;
            return wifi_connect(req.ssid,req.password) ? 0 : -1;
        }
        case SYS_WIFI_DISCONNECT: {
            return wifi_disconnect() ? 0 : -1;
        }
        case SYS_WIFI_STATUS: {
            struct wifi_status_info *out=(struct wifi_status_info*)(uintptr_t)a1;
            if(!writable(out,sizeof(*out))) return -1;
            struct wifi_status st;
            if(!wifi_get_status(&st)) return -1;
            memset(out,0,sizeof(*out));
            out->state=st.state;
            out->connected=st.connected?1:0;
            memcpy(out->ssid,st.ssid,sizeof(out->ssid));
            memcpy(out->bssid,st.bssid,6);
            out->rssi=st.rssi;
            out->channel=st.channel;
            out->security=st.security;
            out->last_error=st.last_error;
            out->ip_address=st.ip_address;
            memcpy(out->mac,st.mac,6);
            memcpy(out->interface_name,st.interface_name,sizeof(out->interface_name));
            out->scan_count=st.scan_count;
            out->last_scan_ms=st.last_scan_ms;
            out->has_device=st.has_device?1:0;
            return 0;
        }
        case SYS_SAVE_KLOG: {
            const struct save_klog_request *user_req=(const struct save_klog_request*)(uintptr_t)a1;
            if(!readable(user_req,sizeof(*user_req))) return -1;
            struct save_klog_request req=*user_req;
            req.device[sizeof(req.device)-1]='\0';
            req.path[sizeof(req.path)-1]='\0';
            for(uint32_t i=0;i<sizeof(req.device);i++) if(!req.device[i]) break; else if(req.device[i]=='\n' || req.device[i]=='\r') req.device[i]='\0';
            for(uint32_t i=0;i<sizeof(req.path);i++) if(!req.path[i]) break; else if(req.path[i]=='\n' || req.path[i]=='\r') req.path[i]='\0';
            if(!req.device[0] || !req.path[0]) return -1;
            return vfs_save_klog_to_device(req.device, req.path);
        }
        case SYS_GET_ROOT_DEVICE: {
            char *buffer=(char*)(uintptr_t)a1;
            uint32_t capacity=(uint32_t)a2;
            if(!writable(buffer,capacity)) return -1;
            const char *root=vfs_root_device_name();
            if(!root || !root[0]) return -1;
            uint32_t len=(uint32_t)strlen(root);
            if(len+1>capacity) return -1;
            memcpy(buffer,root,len+1);
            return (int64_t)len;
        }
        case SYS_GET_FS_TYPE: {
            char *buffer=(char*)(uintptr_t)a1;
            uint32_t capacity=(uint32_t)a2;
            if(!writable(buffer,capacity)) return -1;
            const char *name=vfs_fs_type_name(vfs_root_fs_type());
            uint32_t len=(uint32_t)strlen(name);
            if(len+1>capacity) return -1;
            memcpy(buffer,name,len+1);
            return (int64_t)len;
        }
        case SYS_FAT32_FORMAT_CUSTOM: {
            const struct fat32_custom_format_request *user_req=(const struct fat32_custom_format_request*)(uintptr_t)a1;
            if(!readable(user_req,sizeof(*user_req))) return -1;
            struct fat32_custom_format_request req=*user_req;
            req.device[sizeof(req.device)-1]='\0';
            if(req.partition_count==0 || req.partition_count>4) return -1;
            return fat32_format_custom_device(req.device, req.partition_count, req.sizes_gb);
        }
        case SYS_FORMAT_DEVICE_EX: {
            if(!process_has_capability(PROCESS_CAP_STORAGE_ADMIN)) return -1;
            const struct format_request *req=(const struct format_request*)(uintptr_t)a1;
            if(!readable(req,sizeof(*req))) return -1;
            struct format_request r=*req;
            r.device[sizeof(r.device)-1]='\0'; r.serial[sizeof(r.serial)-1]='\0'; r.erase[sizeof(r.erase)-1]='\0';
            filesystem_syscall_lock();
            int32_t res = vfs_format_device_ex(r.device, r.serial, r.erase, r.fs_type==FS_TYPE_EXT2 ? VFS_FS_EXT2 : VFS_FS_FAT32);
            filesystem_syscall_unlock();
            return res;
        }
        case SYS_EXT2_STAT: {
            const char *path=(const char*)(uintptr_t)a1;
            struct ext2_stat_info *out=(struct ext2_stat_info*)(uintptr_t)a2;
            if(!readable_string(path) || !writable(out,sizeof(*out))) return -1;
            filesystem_syscall_lock();
            int32_t r=vfs_ext2_stat(path,out);
            filesystem_syscall_unlock();
            return r;
        }
        case SYS_EXT2_INODE: {
            uint32_t ino=(uint32_t)a1;
            struct ext2_stat_info *out=(struct ext2_stat_info*)(uintptr_t)a2;
            if(!writable(out,sizeof(*out))) return -1;
            filesystem_syscall_lock();
            int32_t r=vfs_ext2_inode(ino,out);
            filesystem_syscall_unlock();
            return r;
        }
        case SYS_EXT2_SUPER: {
            struct ext2_super_info *out=(struct ext2_super_info*)(uintptr_t)a1;
            if(!writable(out,sizeof(*out))) return -1;
            filesystem_syscall_lock();
            int32_t r=vfs_ext2_super(out);
            filesystem_syscall_unlock();
            return r;
        }
        case SYS_EXT2_BLOCKS: {
            const char *path=(const char*)(uintptr_t)a1;
            struct ext2_blocks_info *out=(struct ext2_blocks_info*)(uintptr_t)a2;
            if(!readable_string(path) || !writable(out,sizeof(*out))) return -1;
            filesystem_syscall_lock();
            int32_t r=vfs_ext2_blocks(path,out);
            filesystem_syscall_unlock();
            return r;
        }
        default:
            serial_write_string("[SYSCALL] unknown n="); print_hex(n); serial_write_string("\n");
            return -1;
    }
}

static uint32_t parse_setting_u32(const char *text){
    uint32_t value=0;
    while(*text>='0' && *text<='9'){
        uint32_t digit=(uint32_t)(*text-'0');
        if(value>(UINT32_MAX-digit)/10U) return UINT32_MAX;
        value=value*10U+digit;
        text++;
    }
    return value;
}

static void audio_restore_system_settings(void){
    int32_t descriptor=vfs_open("/config/settings.ini");
    if(descriptor<0){
        klog(KLOG_INFO,"audio: no saved system settings; detected defaults kept");
        return;
    }
    char buffer[256]={0};
    int32_t amount=vfs_read(descriptor,buffer,sizeof(buffer)-1);
    (void)vfs_close(descriptor);
    if(amount<=0) return;
    buffer[amount]='\0';
    struct audio_status detected={0};
    audio_get_status(&detected);
    uint32_t volume=detected.volume;
    uint32_t device=detected.selected_output_device;
    bool muted=detected.muted!=0;
    for(char *line=buffer;*line;){
        char *end=line;
        while(*end && *end!='\n' && *end!='\r') end++;
        char saved=*end;
        *end='\0';
        if(strncmp(line,"volume=",7)==0) volume=parse_setting_u32(line+7);
        else if(strncmp(line,"muted=",6)==0)
            muted=parse_setting_u32(line+6)!=0;
        else if(strncmp(line,"device=",7)==0)
            device=parse_setting_u32(line+7);
        if(!saved) break;
        line=end+1;
        while(*line=='\n' || *line=='\r') line++;
    }
    if(volume>100) volume=100;
    if(device<detected.output_device_count)
        (void)audio_select_output_device(device);
    audio_set_volume((uint8_t)volume);
    audio_set_muted(muted);
    klogf(KLOG_OK,"audio: restored settings volume=%u mute=%u device=%u",
          volume,muted ? 1 : 0,device);
}

void syscall_init(void){
    static bool filesystem_checked;
    static bool audio_checked;
    klog(KLOG_DEBUG, "syscall: int 0x80 handler ready (DPL3)");
    if(!audio_checked){
        audio_checked=true;
        bool screen_logging = klog_is_screen_enabled();
        klog_set_screen_enabled(false);
        audio_init();
        klog_set_screen_enabled(screen_logging);
        klog(KLOG_INFO, "audio: PC speaker backend ready");
    }
    if(!filesystem_checked){
        filesystem_checked=true;
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage: scanning PCI controllers");
        storage_probe_init();
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage: initializing block transports");
        (void)block_device_init();
        struct ahci_probe_stats ahci_stats;
        struct xhci_probe_stats xhci_stats;
        struct ehci_probe_stats ehci_stats;
        ahci_get_probe_stats(&ahci_stats);
        xhci_get_probe_stats(&xhci_stats);
        ehci_get_probe_stats(&ehci_stats);
        klogf(KLOG_INFO,"pci: %u storage/USB controller(s) detected",
              storage_controller_count());
        klogf(KLOG_INFO,"storage: %u operational disk(s) detected",block_device_count());
        klogf(KLOG_INFO,"ahci: %u SATA disk(s) identified",ahci_device_count());
        klogf(KLOG_DEBUG,"ahci: controllers=%u ports=%u sata=%u identify_failures=%u",
              ahci_stats.controllers,ahci_stats.implemented_ports,
              ahci_stats.sata_ports,ahci_stats.identify_failures);
        klogf(KLOG_INFO,"xhci: controllers=%u connected=%u addressed=%u usb-storage=%u failures=%u stage=%u",
              xhci_stats.controllers,xhci_stats.connected_ports,
              xhci_stats.addressed_devices,xhci_stats.mass_storage_devices,
              xhci_stats.failures,xhci_stats.last_stage);
        klogf(KLOG_INFO,"ehci: controllers=%u connected=%u high-speed=%u usb-storage=%u failures=%u stage=%u",
              ehci_stats.controllers,ehci_stats.connected_ports,
              ehci_stats.high_speed_ports,ehci_stats.mass_storage_devices,
              ehci_stats.failures,ehci_stats.last_stage);
        klog(KLOG_DEBUG,"usb stages: 1=pci 2=running 3=port 4=addressed 5=bulk-endpoints 6=scsi 7=ready");
        if(vfs_mount_root()){
            klogf(KLOG_OK,"vfs: root mounted from %s",vfs_root_device_name());
            audio_restore_system_settings();
            {
                int32_t fd=vfs_open("/purec/install.cfg");
                if(fd>=0){
                    char cfg_buf[512]={0};
                    int32_t r=vfs_read(fd,cfg_buf,sizeof(cfg_buf)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        klogf(KLOG_INFO,"config: /purec/install.cfg (%d bytes)",r);
                        cfg_buf[r]=0;
                        char *p=cfg_buf;
                        while(*p){
                            char line[128]={0};
                            uint32_t i=0;
                            while(*p && *p!='\n' && i<127){ line[i++]=*p++; }
                            if(*p=='\n') p++;
                            if(line[0]) klogf(KLOG_INFO,"  cfg: %s",line);
                        }
                    }
                }
                fd=vfs_open("/etc/hostname");
                if(fd>=0){
                    char hn[64]={0};
                    int32_t r=vfs_read(fd,hn,sizeof(hn)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        hn[r]=0;
                        for(int i=0;i<r;i++) if(hn[i]=='\n'||hn[i]=='\r'){ hn[i]=0; break; }
                        klogf(KLOG_INFO,"config: hostname='%s'",hn);
                    }
                }
                fd=vfs_open("/boot/loader.cfg");
                if(fd>=0){
                    char lb[256]={0};
                    int32_t r=vfs_read(fd,lb,sizeof(lb)-1);
                    (void)vfs_close(fd);
                    if(r>0){
                        lb[r]=0;
                        klogf(KLOG_DEBUG,"config: /boot/loader.cfg %d bytes",r);
                    }
                }
            }
        } else {
            klog(KLOG_WARN,"vfs: no PURECOS FAT32 root found (use 'install' or 'mkfs.fat32' to create)");
        }
        boot_diag_checkpoint(BOOT_STAGE_SYSCALLS, "storage and filesystem probe complete");
    }
}
