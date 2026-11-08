/* USB keyboard support for the Xbox build (lobby chat).
 *
 * nxdk's SDL has no keyboard events, so a USB keyboard (through a controller
 * port adapter) is read here with libusbohci's HID class driver in boot
 * protocol: 8-byte reports [modifiers, reserved, up to 6 key usages]. The keys
 * are turned into DirectInput scan codes, the form the game reads from
 * g_diKeyboardState, and merged into the key state each frame.
 *
 * Only boot keyboards are claimed (interface subclass 1, protocol 1); other
 * HID devices are left alone. Xbox pads are XID devices, not HID, so they
 * never reach this driver. */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <usbh_lib.h>
#include <usbh_hid.h>

static volatile unsigned char s_kbState[256];
static volatile int s_kbCount;
static volatile int s_kbLogged;

/* HID usage (page 7) -> DirectInput scan code. 0 = not mapped. */
static const unsigned char s_hid2dik[0x68] = {
    /* 0x00 */ 0, 0, 0, 0,
    /* 0x04 a-z */
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
    0x15, 0x2C,
    /* 0x1E 1-9,0 */
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    /* 0x28 enter esc backspace tab space - = [ ] \ (nonUS#) ; ' ` , . / */
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27,
    0x28, 0x29, 0x33, 0x34, 0x35,
    /* 0x39 capslock, 0x3A-0x45 F1-F12 */
    0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57,
    0x58,
    /* 0x46 prtsc scrlk pause insert home pgup delete end pgdn right left down up */
    0xB7, 0x46, 0xC5, 0xD2, 0xC7, 0xC9, 0xD3, 0xCF, 0xD1, 0xCD, 0xCB, 0xD0,
    0xC8,
    /* 0x53 numlock kp/ kp* kp- kp+ kpenter kp1-9 kp0 kp. */
    0x45, 0xB5, 0x37, 0x4A, 0x4E, 0x9C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D,
    0x47, 0x48, 0x49, 0x52, 0x53,
    /* 0x64 nonUS \ , app, power, kp= */
    0x56, 0xDD, 0, 0x8D,
};

static void kb_report(HID_DEV_T *hdev, uint16_t ep_addr, int status,
                      uint8_t *rdata, uint32_t data_len)
{
    unsigned char ns[256];
    (void)hdev; (void)ep_addr;
    if (status < 0 || rdata == NULL || data_len < 3) {
        return;
    }
    memset(ns, 0, sizeof(ns));
    uint8_t mod = rdata[0];
    if (mod & 0x01) ns[0x1D] = 0x80;   /* left ctrl */
    if (mod & 0x02) ns[0x2A] = 0x80;   /* left shift */
    if (mod & 0x04) ns[0x38] = 0x80;   /* left alt */
    if (mod & 0x10) ns[0x9D] = 0x80;   /* right ctrl */
    if (mod & 0x20) ns[0x36] = 0x80;   /* right shift */
    if (mod & 0x40) ns[0xB8] = 0x80;   /* right alt */
    for (uint32_t i = 2; i < data_len && i < 8; i++) {
        uint8_t u = rdata[i];
        if (u == 1) {                   /* rollover error: keep the old state */
            return;
        }
        if (u < sizeof(s_hid2dik) && s_hid2dik[u]) {
            ns[s_hid2dik[u]] = 0x80;
        }
    }
    for (int k = 0; k < 256; k++) {
        s_kbState[k] = ns[k];
    }
}

static void kb_connect(HID_DEV_T *hdev, int param)
{
    (void)param;
    if (hdev == NULL || hdev->bSubClassCode != 1 || hdev->bProtocolCode != 1) {
        return;                         /* not a boot keyboard */
    }
    usbh_hid_set_protocol(hdev, 0);    /* boot protocol: fixed 8-byte reports */
    if (usbh_hid_start_int_read(hdev, 0, kb_report) == 0) {
        hdev->user_data = (void *)1;
        s_kbCount++;
        s_kbLogged = 0;
    }
}

static void kb_disconnect(HID_DEV_T *hdev, int param)
{
    (void)param;
    if (hdev != NULL && hdev->user_data) {
        hdev->user_data = NULL;
        if (s_kbCount > 0) s_kbCount--;
        for (int k = 0; k < 256; k++) {
            s_kbState[k] = 0;
        }
        s_kbLogged = 0;
    }
}

/* Must run before SDL initialises its game controllers: drivers have to be
 * registered before the USB devices are enumerated. */
void xbox_kbd_init(void)
{
    usbh_core_init();
    usbh_hid_init();
    usbh_install_hid_conn_callback(kb_connect, kb_disconnect);
}

int xbox_kbd_present(void)
{
    return s_kbCount > 0;
}

/* Called every time the game copies the key state: the keyboard is the only
 * source of key presses on the Xbox (the lobby adds its pad F-keys later). */
void xbox_kbd_merge(unsigned char *keys)
{
    if (!s_kbLogged) {
        s_kbLogged = 1;
        fprintf(stderr, "keyboard: %s\n", s_kbCount > 0 ? "USB keyboard connected" : "none");
    }
    if (s_kbCount <= 0) {
        return;
    }
    for (int k = 0; k < 256; k++) {
        keys[k] |= s_kbState[k];
    }
}
