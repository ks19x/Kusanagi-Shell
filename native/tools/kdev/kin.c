// kin: test-input injector for the kt headless compositors (see kdev/kin, the wrapper that
// builds this and points it at a slot). Drives a zwlr_virtual_pointer_v1 and a
// zwp_virtual_keyboard_v1 (keymap compiled with xkbcommon: pc+us, plus spare keycodes for any
// keysym `type` needs that us doesn't have). Commands run in order in one connection, so
// `kin 3 move 10 10 wait 300 click 20 20` works.
#define _GNU_SOURCE
#include <ctype.h>
#include <poll.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

static struct wl_display* dpy;
static struct wl_seat* seat;
static struct wl_output* output;
static struct zwlr_virtual_pointer_manager_v1* vpm;
static uint32_t vpm_version;
static struct zwp_virtual_keyboard_manager_v1* vkm;
static struct zwlr_virtual_pointer_v1* ptr;
static struct zwp_virtual_keyboard_v1* kbd;
static int out_w = 1920, out_h = 1080;
static bool list_globals;
static int step_ms = 12;

static struct xkb_context* xctx;
static struct xkb_keymap* keymap;
static char* extra_syms; // "key <Ixxx> {[ sym ]};" lines appended to the symbols section

// in the server a bad command fails that request only: die() jumps back to the request loop
static jmp_buf* die_jump;
static char die_msg[256];

static void die(const char* fmt, const char* a) {
  char m[200];
  snprintf(m, sizeof m, fmt, a);
  snprintf(die_msg, sizeof die_msg, "kin: %s\n", m);
  if (die_jump) longjmp(*die_jump, 1);
  fputs(die_msg, stderr);
  exit(1);
}

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void pause_ms(int ms) {
  wl_display_flush(dpy);
  if (ms > 0) usleep(ms * 1000);
}

// Registry
static void out_geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
                         const char* mk, const char* md, int32_t tr) {}
static void out_mode(void* d, struct wl_output* o, uint32_t flags, int32_t w, int32_t h, int32_t r) {
  if (flags & WL_OUTPUT_MODE_CURRENT) { out_w = w; out_h = h; }
}
static void out_done(void* d, struct wl_output* o) {}
static void out_scale(void* d, struct wl_output* o, int32_t s) {}
static void out_name(void* d, struct wl_output* o, const char* n) {}
static void out_desc(void* d, struct wl_output* o, const char* n) {}
static const struct wl_output_listener out_listener = {out_geometry, out_mode, out_done, out_scale, out_name, out_desc};

static void reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
  if (list_globals) printf("%-48s v%u\n", iface, ver);
  if (!strcmp(iface, wl_seat_interface.name) && !seat)
    seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
  else if (!strcmp(iface, wl_output_interface.name) && !output) {
    output = wl_registry_bind(r, name, &wl_output_interface, ver < 4 ? ver : 4);
    wl_output_add_listener(output, &out_listener, NULL);
  } else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name)) {
    vpm_version = ver < 2 ? ver : 2;
    vpm = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, vpm_version);
  } else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name))
    vkm = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}
static void reg_remove(void* d, struct wl_registry* r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {reg_global, reg_remove};

// Pointer
static void need_ptr(void) {
  if (ptr) return;
  if (!vpm) die("%s", "compositor has no zwlr_virtual_pointer_manager_v1");
  ptr = vpm_version >= 2 ? zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(vpm, seat, output)
                         : zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vpm, seat);
  wl_display_roundtrip(dpy);
}

static int cur_x = -1, cur_y = -1;

static void move_to(int x, int y) {
  need_ptr();
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= out_w) x = out_w - 1;
  if (y >= out_h) y = out_h - 1;
  zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), x, y, out_w, out_h);
  zwlr_virtual_pointer_v1_frame(ptr);
  cur_x = x;
  cur_y = y;
  pause_ms(step_ms);
}

// glide in small steps so surfaces see enter/motion like a real mouse (hover states, drags)
static void glide_to(int x, int y, int steps) {
  if (cur_x < 0) { move_to(x, y); return; }
  int x0 = cur_x, y0 = cur_y;
  for (int i = 1; i <= steps; ++i) move_to(x0 + (x - x0) * i / steps, y0 + (y - y0) * i / steps);
}

static void button(uint32_t b, bool down) {
  need_ptr();
  zwlr_virtual_pointer_v1_button(ptr, now_ms(), b, down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
  zwlr_virtual_pointer_v1_frame(ptr);
  pause_ms(step_ms * 3);
}

static void scroll(int dir, int n, bool horizontal) {
  need_ptr();
  uint32_t axis = horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL;
  for (int i = 0; i < n; ++i) {
    zwlr_virtual_pointer_v1_axis_source(ptr, WL_POINTER_AXIS_SOURCE_WHEEL);
    zwlr_virtual_pointer_v1_axis_discrete(ptr, now_ms(), axis, wl_fixed_from_int(15 * dir), dir);
    zwlr_virtual_pointer_v1_frame(ptr);
    pause_ms(step_ms * 4);
  }
}

static uint32_t parse_button(const char* s) {
  if (!strcmp(s, "left")) return BTN_LEFT;
  if (!strcmp(s, "right")) return BTN_RIGHT;
  if (!strcmp(s, "middle")) return BTN_MIDDLE;
  return 0;
}

// Keyboard
static void upload_keymap(void) {
  char* str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  size_t size = strlen(str) + 1;
  int fd = memfd_create("kin-keymap", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size) < 0) die("%s", "memfd");
  char* map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  memcpy(map, str, size);
  munmap(map, size);
  zwp_virtual_keyboard_v1_keymap(kbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
  close(fd);
  free(str);
  wl_display_roundtrip(dpy);
}

static void compile_keymap(void) {
  // pc+us like a default desktop keyboard; spare evdev "I" keycodes carry the extra keysyms
  size_t len = 512 + (extra_syms ? strlen(extra_syms) : 0);
  char* src = malloc(len);
  snprintf(src, len,
           "xkb_keymap {\n"
           "  xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
           "  xkb_types { include \"complete\" };\n"
           "  xkb_compat { include \"complete\" };\n"
           "  xkb_symbols { include \"pc+us+inet(evdev)\"\n%s  };\n"
           "};\n",
           extra_syms ? extra_syms : "");
  struct xkb_keymap* km = xkb_keymap_new_from_string(xctx, src, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
  free(src);
  if (!km) die("%s", "could not compile keymap");
  if (keymap) xkb_keymap_unref(keymap);
  keymap = km;
}

static void need_kbd(void) {
  if (kbd) return;
  if (!vkm) die("%s", "compositor has no zwp_virtual_keyboard_manager_v1");
  xctx = xkb_context_new(0);
  compile_keymap();
  kbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkm, seat);
  upload_keymap();
}

// find a keycode + the modifiers (shift / altgr) that produce sym on layout 0
static bool lookup(xkb_keysym_t sym, xkb_keycode_t* code, xkb_mod_mask_t* mods) {
  xkb_keycode_t lo = xkb_keymap_min_keycode(keymap), hi = xkb_keymap_max_keycode(keymap);
  for (int pass = 0; pass < 2; ++pass) // prefer level 1 (no modifiers) across all keys first
    for (xkb_keycode_t k = lo; k <= hi; ++k) {
      int levels = xkb_keymap_num_levels_for_key(keymap, k, 0);
      for (int l = pass == 0 ? 0 : 1; l < (pass == 0 ? (levels > 0 ? 1 : 0) : levels); ++l) {
        const xkb_keysym_t* syms;
        int n = xkb_keymap_key_get_syms_by_level(keymap, k, 0, l, &syms);
        if (n != 1 || syms[0] != sym) continue;
        xkb_mod_mask_t masks[8];
        size_t nm = xkb_keymap_key_get_mods_for_level(keymap, k, 0, l, masks, 8);
        if (nm == 0) continue;
        *code = k;
        *mods = masks[0];
        return true;
      }
    }
  return false;
}

// give sym a keycode the layout leaves empty (recompiles + re-uploads the keymap)
static void add_spare(xkb_keysym_t sym) {
  const char* kname = NULL;
  for (xkb_keycode_t k = xkb_keymap_max_keycode(keymap); k > 8 && !kname; --k)
    if (xkb_keymap_num_layouts_for_key(keymap, k) == 0 && xkb_keymap_key_get_name(keymap, k)) kname = xkb_keymap_key_get_name(keymap, k);
  if (!kname) die("%s", "no spare keycode left for unusual characters");
  char name[64], line[160];
  xkb_keysym_get_name(sym, name, sizeof name);
  snprintf(line, sizeof line, "    key <%s> { [ %s ] };\n", kname, name);
  size_t old = extra_syms ? strlen(extra_syms) : 0;
  extra_syms = realloc(extra_syms, old + strlen(line) + 1);
  strcpy(extra_syms + old, line);
  compile_keymap();
  upload_keymap();
}

static void raw_key(xkb_keycode_t code, bool down) {
  zwp_virtual_keyboard_v1_key(kbd, now_ms(), code - 8, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  pause_ms(step_ms);
}

static xkb_keycode_t keycode_of(const char* keysym_name) {
  xkb_keycode_t c;
  xkb_mod_mask_t m;
  xkb_keysym_t s = xkb_keysym_from_name(keysym_name, 0);
  if (s == XKB_KEY_NoSymbol || !lookup(s, &c, &m)) die("no key for %s", keysym_name);
  return c;
}

// press the real modifier keys so both the compositor and the client see them
static int mod_keys(xkb_mod_mask_t mask, xkb_keycode_t* out) {
  int n = 0;
  if (mask & (1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT))) out[n++] = keycode_of("Shift_L");
  if (mask & (1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL))) out[n++] = keycode_of("Control_L");
  if (mask & (1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT))) out[n++] = keycode_of("Alt_L");
  if (mask & (1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO))) out[n++] = keycode_of("Super_L");
  xkb_mod_index_t m5 = xkb_keymap_mod_get_index(keymap, "Mod5");
  if (m5 != XKB_MOD_INVALID && (mask & (1u << m5))) out[n++] = keycode_of("ISO_Level3_Shift");
  return n;
}

// mango doesn't derive modifier state from a virtual keyboard's key events: send it explicitly too
static void send_mods(xkb_mod_mask_t mods) {
  zwp_virtual_keyboard_v1_modifiers(kbd, mods, 0, 0, 0);
  pause_ms(step_ms);
}

static void tap(xkb_keycode_t code, xkb_mod_mask_t mods) {
  xkb_keycode_t mk[8];
  int n = mod_keys(mods, mk);
  for (int i = 0; i < n; ++i) raw_key(mk[i], true);
  if (n) send_mods(mods);
  raw_key(code, true);
  raw_key(code, false);
  for (int i = n - 1; i >= 0; --i) raw_key(mk[i], false);
  if (n) send_mods(0);
}

static bool parse_mods(const char* s, xkb_mod_mask_t* out) {
  char buf[128];
  snprintf(buf, sizeof buf, "%s", s);
  xkb_mod_mask_t m = 0;
  for (char* t = strtok(buf, "+,"); t; t = strtok(NULL, "+,")) {
    for (char* p = t; *p; ++p) *p = tolower(*p);
    const char* name = NULL;
    if (!strcmp(t, "shift")) name = XKB_MOD_NAME_SHIFT;
    else if (!strcmp(t, "ctrl") || !strcmp(t, "control")) name = XKB_MOD_NAME_CTRL;
    else if (!strcmp(t, "alt")) name = XKB_MOD_NAME_ALT;
    else if (!strcmp(t, "super") || !strcmp(t, "logo") || !strcmp(t, "mod4") || !strcmp(t, "win")) name = XKB_MOD_NAME_LOGO;
    else return false;
    m |= 1u << xkb_keymap_mod_get_index(keymap, name);
  }
  *out = m;
  return true;
}

static void key_cmd(const char* name, xkb_mod_mask_t mods) {
  need_kbd();
  xkb_keysym_t sym = xkb_keysym_from_name(name, 0);
  if (sym == XKB_KEY_NoSymbol) sym = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
  if (sym == XKB_KEY_NoSymbol) die("unknown keysym '%s'", name);
  xkb_keycode_t code;
  xkb_mod_mask_t level_mods;
  if (!lookup(sym, &code, &level_mods)) {
    add_spare(sym);
    if (!lookup(sym, &code, &level_mods)) die("cannot map keysym '%s'", name);
  }
  tap(code, mods | level_mods);
}

static void type_cmd(const char* text) {
  need_kbd();
  for (const unsigned char* p = (const unsigned char*)text; *p;) {
    // decode one UTF-8 code point
    uint32_t cp = *p++;
    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    if (extra) cp &= 0x3F >> extra;
    while (extra-- > 0 && (*p & 0xC0) == 0x80) cp = (cp << 6) | (*p++ & 0x3F);
    xkb_keysym_t sym = cp == '\n' ? XKB_KEY_Return : cp == '\t' ? XKB_KEY_Tab : xkb_utf32_to_keysym(cp);
    if (sym == XKB_KEY_NoSymbol) continue;
    xkb_keycode_t code;
    xkb_mod_mask_t mods;
    if (!lookup(sym, &code, &mods)) {
      add_spare(sym);
      if (!lookup(sym, &code, &mods)) continue;
    }
    tap(code, mods);
  }
}

// Main
static int num(const char* s) {
  char* end;
  long v = strtol(s, &end, 10);
  if (!*s || *end) die("expected a number, got '%s'", s);
  return (int)v;
}

static bool is_num(const char* s) {
  if (!*s) return false;
  for (const char* p = (*s == '-' ? s + 1 : s); *p; ++p)
    if (!isdigit((unsigned char)*p)) return false;
  return true;
}

static void run_commands(int argc, char** argv) {
  for (int i = 0; i < argc;) {
    const char* c = argv[i++];
#define ARG() (i < argc ? argv[i++] : (die("missing argument for '%s'", c), ""))
    if (!strcmp(c, "move")) {
      int x = num(ARG()), y = num(ARG());
      glide_to(x, y, 8);
    } else if (!strcmp(c, "warp")) {
      int x = num(ARG()), y = num(ARG());
      move_to(x, y);
    } else if (!strcmp(c, "click") || !strcmp(c, "dclick")) {
      int x = num(ARG()), y = num(ARG());
      uint32_t b = BTN_LEFT;
      if (i < argc && parse_button(argv[i])) b = parse_button(argv[i++]);
      glide_to(x, y, 4);
      pause_ms(40);
      for (int n = 0; n < (c[0] == 'd' ? 2 : 1); ++n) {
        button(b, true);
        button(b, false);
      }
    } else if (!strcmp(c, "press") || !strcmp(c, "release")) {
      uint32_t b = BTN_LEFT;
      if (i < argc && parse_button(argv[i])) b = parse_button(argv[i++]);
      button(b, c[0] == 'p');
    } else if (!strcmp(c, "scroll") || !strcmp(c, "hscroll")) {
      int x = num(ARG()), y = num(ARG());
      const char* d = ARG();
      int dir = (!strcmp(d, "up") || !strcmp(d, "left")) ? -1 : (!strcmp(d, "down") || !strcmp(d, "right")) ? 1 : 0;
      if (!dir) die("scroll direction must be up|down|left|right, got '%s'", d);
      int n = (i < argc && is_num(argv[i])) ? num(argv[i++]) : 1;
      glide_to(x, y, 4);
      pause_ms(30);
      scroll(dir, n, c[0] == 'h');
    } else if (!strcmp(c, "drag")) {
      int x1 = num(ARG()), y1 = num(ARG()), x2 = num(ARG()), y2 = num(ARG());
      glide_to(x1, y1, 4);
      pause_ms(40);
      button(BTN_LEFT, true);
      int steps = 20;
      for (int s = 1; s <= steps; ++s) {
        move_to(x1 + (x2 - x1) * s / steps, y1 + (y2 - y1) * s / steps);
        pause_ms(8);
      }
      pause_ms(40);
      button(BTN_LEFT, false);
    } else if (!strcmp(c, "key")) {
      const char* k = ARG();
      need_kbd();
      xkb_mod_mask_t mods = 0;
      if (i < argc && parse_mods(argv[i], &mods)) ++i;
      key_cmd(k, mods);
    } else if (!strcmp(c, "type")) {
      type_cmd(ARG());
    } else if (!strcmp(c, "wait")) {
      pause_ms(num(ARG()));
    } else {
      die("unknown command '%s'", c);
    }
  }
  wl_display_roundtrip(dpy);
}

// One connection per slot that keeps its pointer and keyboard, so the seat never loses its pointer
// between commands (hover states and hover-paused timers hold) and clients see one keymap instead of a
// new keyboard per call. Serves NUL-separated argument lists on a unix socket until the compositor goes.
static int serve(const char* path) {
  int ls = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
  unlink(path);
  if (ls < 0 || bind(ls, (struct sockaddr*)&addr, sizeof addr) < 0 || listen(ls, 8) < 0) die("cannot listen on %s", path);
  need_ptr();
  need_kbd();
  for (;;) {
    while (wl_display_prepare_read(dpy) != 0) wl_display_dispatch_pending(dpy);
    wl_display_flush(dpy);
    struct pollfd fds[2] = {{.fd = wl_display_get_fd(dpy), .events = POLLIN}, {.fd = ls, .events = POLLIN}};
    if (poll(fds, 2, -1) < 0) {
      wl_display_cancel_read(dpy);
      continue;
    }
    if (fds[0].revents & POLLIN) {
      if (wl_display_read_events(dpy) < 0) break;
    } else {
      wl_display_cancel_read(dpy);
    }
    if (fds[0].revents & (POLLHUP | POLLERR)) break;
    if (wl_display_dispatch_pending(dpy) < 0) break;
    if (!(fds[1].revents & POLLIN)) continue;
    int c = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
    if (c < 0) continue;
    static char buf[65536];
    size_t len = 0;
    ssize_t n;
    while (len < sizeof buf - 1 && (n = read(c, buf + len, sizeof buf - 1 - len)) > 0) len += (size_t)n;
    buf[len] = 0;
    char* args[512];
    int argc = 0;
    for (size_t off = 0; off < len && argc < 512; off += strlen(buf + off) + 1) args[argc++] = buf + off;
    jmp_buf jb;
    die_msg[0] = 0;
    if (setjmp(jb) == 0) {
      die_jump = &jb;
      run_commands(argc, args);
    }
    die_jump = NULL;
    if (die_msg[0]) (void)!write(c, die_msg, strlen(die_msg));
    close(c);
    if (wl_display_get_error(dpy)) break;
  }
  unlink(path);
  return 0;
}

static int send_to(const char* path, int argc, char** argv) {
  int c = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
  if (c < 0 || connect(c, (struct sockaddr*)&addr, sizeof addr) < 0) return 3;
  for (int i = 0; i < argc; ++i) (void)!write(c, argv[i], strlen(argv[i]) + 1);
  shutdown(c, SHUT_WR);
  char out[512];
  ssize_t n;
  int rc = 0;
  while ((n = read(c, out, sizeof out)) > 0) {
    (void)!write(2, out, (size_t)n);
    rc = 1;
  }
  close(c);
  return rc;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: kin-bin --serve <sock> | --send <sock> <command…> | <command…>  (use the kin wrapper)\n");
    return 2;
  }
  if (!strcmp(argv[1], "--send")) return argc < 3 ? 2 : send_to(argv[2], argc - 3, argv + 3);
  if (!strcmp(argv[1], "globals")) list_globals = true;
  dpy = wl_display_connect(NULL);
  if (!dpy) die("cannot connect to %s", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(null)");
  struct wl_registry* reg = wl_display_get_registry(dpy);
  wl_registry_add_listener(reg, &reg_listener, NULL);
  wl_display_roundtrip(dpy);
  wl_display_roundtrip(dpy);
  if (list_globals) return 0;
  if (!strcmp(argv[1], "--serve")) return argc < 3 ? 2 : serve(argv[2]);

  run_commands(argc - 1, argv + 1);
  pause_ms(30);
  if (kbd) zwp_virtual_keyboard_v1_destroy(kbd);
  if (ptr) zwlr_virtual_pointer_v1_destroy(ptr);
  wl_display_roundtrip(dpy);
  wl_display_disconnect(dpy);
  return 0;
}
