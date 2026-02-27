#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dswifi9.h>
#include <fat.h>
#include <nds.h>

#define nil ((void *)0)

#define DEFAULT_IP "192.168.0.11"
#define DEFAULT_PORT "3923"
#define DEFAULT_PATH "/nds/reader.nds"
#define CONFIG_PATH "fat:/ndsfetch.cfg"
#define CONFIG_PATH2 "sd:/ndsfetch.cfg"
#define RECV_BUF 4096
#define MAX_ROM (4 * 1024 * 1024) // we are actually on the dsi
#define MAX_FILES 32
#define DIR_BUF_SIZE (64 * 1024)

static PrintConsole topScreen;
static PrintConsole bottomScreen;

typedef struct {
  char server_ip[64];
  char server_port[8];
  char rom_path[128];
  char wifi_ssid[33];
  char wifi_pass[65];
} config_t;

static config_t cfg;
static int fat_ok = 0;

typedef struct {
  uint8_t title[12];
  uint8_t gamecode[4];
  uint8_t makercode[2];
  uint8_t unitcode;
  uint8_t seed_select;
  uint8_t capacity;
  uint8_t reserved1[7];
  uint8_t reserved2;
  uint8_t nds_region;
  uint8_t rom_version;
  uint8_t autostart;
  uint32_t arm9_offset;
  uint32_t arm9_entry;
  uint32_t arm9_load;
  uint32_t arm9_size;
  uint32_t arm7_offset;
  uint32_t arm7_entry;
  uint32_t arm7_load;
  uint32_t arm7_size;
} __attribute__((packed)) nds_header_t;

static uint8_t *rom_buf = nil;
static int rom_size = 0;

static void config_defaults(void) {
  strncpy(cfg.server_ip, DEFAULT_IP, sizeof(cfg.server_ip));
  strncpy(cfg.server_port, DEFAULT_PORT, sizeof(cfg.server_port));
  strncpy(cfg.rom_path, DEFAULT_PATH, sizeof(cfg.rom_path));
  cfg.wifi_ssid[0] = '\0';
  cfg.wifi_pass[0] = '\0';
}

static void config_load(void) {
  config_defaults();
  if (!fat_ok)
    return;

  const char *path = CONFIG_PATH2;
  FILE *f = fopen(CONFIG_PATH2, "r");
  if (!f) {
    f = fopen(CONFIG_PATH, "r");
    path = CONFIG_PATH;
  }
  if (!f)
    return;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = '\0';
    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *val = eq + 1;

    if (strcmp(line, "server_ip") == 0)
      strncpy(cfg.server_ip, val, sizeof(cfg.server_ip) - 1);
    else if (strcmp(line, "server_port") == 0)
      strncpy(cfg.server_port, val, sizeof(cfg.server_port) - 1);
    else if (strcmp(line, "rom_path") == 0)
      strncpy(cfg.rom_path, val, sizeof(cfg.rom_path) - 1);
    else if (strcmp(line, "wifi_ssid") == 0)
      strncpy(cfg.wifi_ssid, val, sizeof(cfg.wifi_ssid) - 1);
    else if (strcmp(line, "wifi_pass") == 0)
      strncpy(cfg.wifi_pass, val, sizeof(cfg.wifi_pass) - 1);
  }
  fclose(f);
  printf("loaded config from %s\n", path);
}

static void config_save(void) {
  if (!fat_ok)
    return;

  const char *path = CONFIG_PATH2;
  FILE *f = fopen(CONFIG_PATH2, "w");
  if (!f) {
    f = fopen(CONFIG_PATH, "w");
    path = CONFIG_PATH;
  }
  if (!f)
    return;

  fprintf(f, "server_ip=%s\n", cfg.server_ip);
  fprintf(f, "server_port=%s\n", cfg.server_port);
  fprintf(f, "rom_path=%s\n", cfg.rom_path);
  if (cfg.wifi_ssid[0])
    fprintf(f, "wifi_ssid=%s\n", cfg.wifi_ssid);
  if (cfg.wifi_pass[0])
    fprintf(f, "wifi_pass=%s\n", cfg.wifi_pass);
  fclose(f);
  printf("saved config to %s\n", path);
}

static void on_key_pressed(int key) {
  if (key > 0)
    printf("%c", key);
}

static void kbd_input(const char *prompt, char *buf, int maxlen) {
  consoleSelect(&bottomScreen);
  consoleClear();
  printf("%s\n", prompt);
  printf("Current: %s\n\n", buf);
  printf("Type new value (ENTER to keep):\n");

  Keyboard *kbd = keyboardDemoInit();
  kbd->OnKeyPressed = on_key_pressed;

  char tmp[256] = {0};
  scanf("%255s", tmp);
  keyboardHide();

  if (tmp[0] != '\0') {
    snprintf(buf, (size_t)maxlen, "%s", tmp);
  }

  consoleSelect(&topScreen);
}

static void wait_key(const char *msg) {
  printf("\x1b[31m%s\x1b[0m\n", msg);
  printf("\nPress A to retry, START to reboot\n");
  while (1) {
    swiWaitForVBlank();
    scanKeys();
    u16 k = keysDown();
    if (k & KEY_A)
      return;
    if (k & KEY_START) {
      swiSoftReset();
    }
  }
}

static int wifi_connect(void) {
  consoleSelect(&topScreen);
  consoleClear();

  Wifi_EnableWifi();
  swiWaitForVBlank();

  int num_wfc = Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, nil);
  int has_saved = (cfg.wifi_ssid[0] != '\0');

  consoleClear();
  printf("=== WiFi Setup ===\n\n");
  if (has_saved)
    printf("  A  Saved: %.24s\n", cfg.wifi_ssid);
  else if (num_wfc > 0)
    printf("  A  Connect (firmware AP)\n");
  else
    printf("  -  No saved network\n");
  printf("  B  Scan for networks\n\n");

  int mode = 0;
  while (!mode) {
    cothread_yield_irq(IRQ_VBLANK);
    scanKeys();
    u16 k = keysDown();
    if ((k & KEY_A) && (has_saved || num_wfc > 0))
      mode = has_saved ? 3 : 1;
    if (k & KEY_B)
      mode = 2;
  }

  if (mode == 1) {
    consoleClear();
    printf("Connecting to firmware AP...\n\n");
    Wifi_AutoConnect();
  } else if (mode == 3) {
    consoleClear();
    printf("Connecting to %.24s...\n", cfg.wifi_ssid);

    Wifi_ScanMode();
    int found = 0;
    Wifi_AccessPoint ap;

    for (int tries = 0; tries < 300 && !found; tries++) {
      cothread_yield_irq(IRQ_VBLANK);
      int count = Wifi_GetNumAP();
      for (int i = 0; i < count; i++) {
        Wifi_GetAPData(i, &ap);
        if (strcmp((const char *)ap.ssid, cfg.wifi_ssid) == 0) {
          found = 1;
          break;
        }
      }
    }

    if (!found) {
      printf("Network not found!\n");
      return 0;
    }

    Wifi_SetIP(0, 0, 0, 0, 0);

    if (cfg.wifi_pass[0]) {
      size_t plen = strlen(cfg.wifi_pass);
      printf("Using saved password (%zu chars)\n", plen);
      Wifi_ConnectSecureAP(&ap, cfg.wifi_pass, plen);
    } else {
      Wifi_ConnectSecureAP(&ap, nil, 0);
    }
  } else {
    Wifi_ScanMode();
    int chosen = 0;
    Wifi_AccessPoint ap;

    consoleClear();
    printf("Scanning...\n");

    int picked = 0;
    while (!picked) {
      cothread_yield_irq(IRQ_VBLANK);
      scanKeys();
      u16 k = keysDown();

      int count = Wifi_GetNumAP();
      if (count == 0)
        continue;

      if (k & KEY_UP && chosen > 0)
        chosen--;
      if (k & KEY_DOWN && chosen < count - 1)
        chosen++;

      consoleClear();

      int first = chosen > 3 ? chosen - 3 : 0;
      int last = first + 6;
      if (last >= count)
        last = count - 1;

      for (int i = first; i <= last; i++) {
        Wifi_AccessPoint tmp;
        Wifi_GetAPData(i, &tmp);

        int bars = tmp.rssi > 180   ? 4
                   : tmp.rssi > 140 ? 3
                   : tmp.rssi > 100 ? 2
                                    : 1;
        char signal[5] = "....";
        for (int b = 0; b < bars; b++)
          signal[b] = '|';

        const char *sec = Wifi_ApSecurityTypeString(tmp.security_type);
        int wfc = (tmp.flags & WFLAG_APDATA_CONFIG_IN_WFC);

        if (i == chosen) {
          printf("\x1b[33m> %-20.20s %s\x1b[0m\n", tmp.ssid, signal);
          printf("\x1b[33m  %s ch%d%s\x1b[0m\n", sec, tmp.channel,
                 wfc ? " *" : "");
        } else {
          printf("  %-20.20s %s\n", tmp.ssid, signal);
          printf("  %s ch%d%s\n", sec, tmp.channel, wfc ? " *" : "");
        }
      }

      printf("\n%d networks  (* = saved)\n", count);
      printf("[UP/DOWN] select  [A] connect\n");

      if (k & KEY_A) {
        Wifi_GetAPData(chosen, &ap);
        if (ap.flags & WFLAG_APDATA_COMPATIBLE) {
          picked = 1;
        } else {
          consoleClear();
          printf("Incompatible network!\n");
          printf("(DS only supports open/WEP,\n");
          printf(" DSi adds WPA2)\n\n");
          printf("Press any key...\n");
          while (1) {
            cothread_yield_irq(IRQ_VBLANK);
            scanKeys();
            if (keysDown())
              break;
          }
        }
      }
    }

    consoleClear();
    printf("Network: %.24s\n", ap.ssid);
    printf("Type:    %s\n\n", Wifi_ApSecurityTypeString(ap.security_type));

    Wifi_SetIP(0, 0, 0, 0, 0);

    if (ap.flags & WFLAG_APDATA_CONFIG_IN_WFC) {
      printf("Using saved credentials...\n");
      Wifi_ConnectWfcAP(&ap);
      snprintf(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "%s",
               (const char *)ap.ssid);
      cfg.wifi_pass[0] = '\0';
      config_save();
    } else if (ap.security_type == AP_SECURITY_OPEN) {
      printf("Open network, connecting...\n");
      Wifi_ConnectSecureAP(&ap, nil, 0);
      snprintf(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "%s",
               (const char *)ap.ssid);
      cfg.wifi_pass[0] = '\0';
      config_save();
    } else {
      printf("Password: ");
      Keyboard *kbd = keyboardDemoInit();
      kbd->OnKeyPressed = on_key_pressed;
      char password[65] = {0};
      scanf("%64s", password);
      keyboardHide();
      size_t plen = strlen(password);
      printf("\n\nConnecting (%zu chars)...\n", plen);
      Wifi_ConnectSecureAP(&ap, password, plen);
      snprintf(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "%s",
               (const char *)ap.ssid);
      snprintf(cfg.wifi_pass, sizeof(cfg.wifi_pass), "%s", password);
      config_save();
    }
  }

  consoleClear();
  printf("Connecting...\n\n");

  int oldstatus = -1;
  int timeout = 60 * 10;

  while (timeout-- > 0) {
    cothread_yield_irq(IRQ_VBLANK);
    scanKeys();

    if (keysDown() & KEY_B) {
      printf("Cancelled.\n");
      return 0;
    }

    int status = Wifi_AssocStatus();
    if (status != oldstatus) {
      printf("  %s\n", ASSOCSTATUS_STRINGS[status]);
      oldstatus = status;
    }

    if (status == ASSOCSTATUS_CANNOTCONNECT) {
      printf("\nCannot connect!\n");
      return 0;
    }

    if (status == ASSOCSTATUS_ASSOCIATED)
      break;
  }

  if (timeout <= 0) {
    printf("Connection timed out.\n");
    return 0;
  }

  struct in_addr ip = {0}, gw = {0}, mask = {0}, dns1 = {0}, dns2 = {0};
  ip = Wifi_GetIPInfo(&gw, &mask, &dns1, &dns2);
  printf("\n\x1b[32mConnected!\x1b[0m\n");
  printf("IP: %s\n", inet_ntoa(ip));
  return 1;
}

static int http_get(const char *host, const char *port, const char *path) {
  printf("Connecting to %s:%s%s\n", host, port, path);

  struct addrinfo hint = {0};
  hint.ai_family = AF_INET;
  hint.ai_socktype = SOCK_STREAM;

  struct addrinfo *result = nil;
  int err = getaddrinfo(host, port, &hint, &result);
  if (err != 0) {
    printf("getaddrinfo: %d\n", err);
    return 0;
  }

  int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (fd < 0) {
    perror("socket");
    freeaddrinfo(result);
    return 0;
  }

  if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
    perror("connect");
    close(fd);
    freeaddrinfo(result);
    return 0;
  }
  freeaddrinfo(result);
  printf("Connected!\n");

  char req[512];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\n"
           "Host: %s:%s\r\n"
           "User-Agent: ndsfetch\r\n"
           "Connection: close\r\n\r\n",
           path, host, port);

  if (write(fd, req, strlen(req)) < 0) {
    perror("write");
    close(fd);
    return 0;
  }

  int opt = 1;
  ioctl(fd, FIONBIO, (char *)&opt);

  static char hdr_buf[2048];
  int hdr_len = 0;
  int content_length = -1;
  int header_done = 0;

  rom_buf = (uint8_t *)malloc(MAX_ROM);
  if (!rom_buf) {
    printf("malloc(%d) failed!\n", MAX_ROM);
    close(fd);
    return 0;
  }
  rom_size = 0;

  while (1) {
    char tmp[RECV_BUF];
    int n = read(fd, tmp, sizeof(tmp));

    if (n > 0) {
      if (!header_done) {
        int space = (int)sizeof(hdr_buf) - hdr_len - 1;
        int copy = n < space ? n : space;
        memcpy(hdr_buf + hdr_len, tmp, (size_t)copy);
        hdr_len += copy;
        hdr_buf[hdr_len] = '\0';

        char *end = strstr(hdr_buf, "\r\n\r\n");
        if (end) {
          header_done = 1;
          int hdr_total = (int)(end - hdr_buf) + 4;

          // check HTTP status code
          if (strncmp(hdr_buf, "HTTP/", 5) == 0) {
            char *sp = strchr(hdr_buf, ' ');
            if (sp) {
              int status = atoi(sp + 1);
              printf("HTTP %d\n", status);
              if (status != 200) {
                printf("HTTP error %d!\n", status);
                free(rom_buf);
                rom_buf = nil;
                close(fd);
                return 0;
              }
            }
          }

          char *cl = strstr(hdr_buf, "Content-Length: ");
          if (!cl)
            cl = strstr(hdr_buf, "content-length: ");
          if (cl) {
            cl += strlen("Content-Length: ");
            content_length = atoi(cl);
            printf("ROM size: %d bytes\n", content_length);
            if (content_length > MAX_ROM) {
              printf("ROM too large!\n");
              free(rom_buf);
              rom_buf = nil;
              close(fd);
              return 0;
            }
          }

          int body_start = hdr_total;
          int body_bytes = hdr_len - body_start;
          if (body_bytes > 0) {
            memcpy(rom_buf, hdr_buf + body_start, (size_t)body_bytes);
            rom_size = body_bytes;
          }
        }
      } else {
        if (rom_size + n > MAX_ROM) {
          printf("ROM too large!\n");
          free(rom_buf);
          rom_buf = nil;
          close(fd);
          return 0;
        }
        memcpy(rom_buf + rom_size, tmp, (size_t)n);
        rom_size += n;
      }

      if (header_done) {
        int pct = content_length > 0 ? (rom_size * 100) / content_length : 0;
        printf("\r  %d / %d bytes (%d%%)", rom_size, content_length, pct);
      }
    } else if (n == 0) {
      break;
    }

    if (content_length > 0 && rom_size >= content_length)
      break;

    cothread_yield();
  }

  printf("\n");
  close(fd);

  if (rom_size < (int)sizeof(nds_header_t)) {
    printf("Download too small: %d bytes\n", rom_size);
    free(rom_buf);
    rom_buf = nil;
    return 0;
  }

  printf("Download complete: %d bytes\n", rom_size);
  return 1;
}

static int browse_server(void) {
  // extract directory from rom_path
  char dir[128];
  snprintf(dir, sizeof(dir), "%s", cfg.rom_path);
  char *last_slash = strrchr(dir, '/');
  if (last_slash)
    *(last_slash + 1) = '\0';
  else
    snprintf(dir, sizeof(dir), "/");

  consoleClear();
  printf("Listing %s ...\n", dir);

  struct addrinfo hint = {0};
  hint.ai_family = AF_INET;
  hint.ai_socktype = SOCK_STREAM;

  struct addrinfo *result = nil;
  int err = getaddrinfo(cfg.server_ip, cfg.server_port, &hint, &result);
  if (err != 0) {
    printf("getaddrinfo: %d\n", err);
    return 0;
  }

  int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (fd < 0) {
    perror("socket");
    freeaddrinfo(result);
    return 0;
  }

  if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
    perror("connect");
    close(fd);
    freeaddrinfo(result);
    return 0;
  }
  freeaddrinfo(result);

  char req[512];
  snprintf(req, sizeof(req),
           "GET %s HTTP/1.1\r\n"
           "Host: %s:%s\r\n"
           "User-Agent: ndsfetch\r\n"
           "Connection: close\r\n\r\n",
           dir, cfg.server_ip, cfg.server_port);

  if (write(fd, req, strlen(req)) < 0) {
    perror("write");
    close(fd);
    return 0;
  }

  int opt = 1;
  ioctl(fd, FIONBIO, (char *)&opt);

  char *body = (char *)malloc(DIR_BUF_SIZE);
  if (!body) {
    printf("malloc failed\n");
    close(fd);
    return 0;
  }

  int total = 0;
  int header_done = 0;
  char hdr_buf[2048];
  int hdr_len = 0;

  while (total < DIR_BUF_SIZE - 1) {
    char tmp[RECV_BUF];
    int n = read(fd, tmp, sizeof(tmp));

    if (n > 0) {
      if (!header_done) {
        int space = (int)sizeof(hdr_buf) - hdr_len - 1;
        int copy = n < space ? n : space;
        memcpy(hdr_buf + hdr_len, tmp, (size_t)copy);
        hdr_len += copy;
        hdr_buf[hdr_len] = '\0';

        char *end = strstr(hdr_buf, "\r\n\r\n");
        if (end) {
          header_done = 1;
          int hdr_total = (int)(end - hdr_buf) + 4;
          int body_bytes = hdr_len - hdr_total;
          if (body_bytes > 0) {
            memcpy(body, hdr_buf + hdr_total, (size_t)body_bytes);
            total = body_bytes;
          }
        }
      } else {
        int space = DIR_BUF_SIZE - 1 - total;
        int copy = n < space ? n : space;
        memcpy(body + total, tmp, (size_t)copy);
        total += copy;
      }
    } else if (n == 0) {
      break;
    }

    cothread_yield();
  }

  close(fd);
  body[total] = '\0';

  // parse href="..." for .nds files
  char files[MAX_FILES][128];
  int nfiles = 0;

  char *p = body;
  while ((p = strstr(p, "href=\"")) != nil && nfiles < MAX_FILES) {
    p += 6;
    char *end = strchr(p, '"');
    if (!end)
      break;

    int len = (int)(end - p);

    // look for .nds extension (possibly followed by query string)
    char *nds = nil;
    for (char *s = p; s + 4 <= end; s++) {
      if (memcmp(s, ".nds", 4) == 0) {
        nds = s;
        break;
      }
    }

    if (nds) {
      int flen = (int)(nds - p) + 4;
      if (flen > 0 && flen < (int)sizeof(files[0]) - 1) {
        memcpy(files[nfiles], p, (size_t)flen);
        files[nfiles][flen] = '\0';
        nfiles++;
      }
    }

    p = end + 1;
    (void)len;
  }

  free(body);

  if (nfiles == 0) {
    printf("No .nds files found in %s\n", dir);
    return 0;
  }

  int chosen = 0;

  while (1) {
    cothread_yield_irq(IRQ_VBLANK);
    scanKeys();
    u16 k = keysDown();

    if (k & KEY_UP && chosen > 0)
      chosen--;
    if (k & KEY_DOWN && chosen < nfiles - 1)
      chosen++;

    consoleClear();
    printf("=== %s ===\n\n", dir);

    int first = chosen > 5 ? chosen - 5 : 0;
    int last = first + 11;
    if (last >= nfiles)
      last = nfiles - 1;

    for (int i = first; i <= last; i++) {
      if (i == chosen)
        printf("\x1b[33m> %s\x1b[0m\n", files[i]);
      else
        printf("  %s\n", files[i]);
    }

    printf("\n%d files  [A] select  [B] cancel\n", nfiles);

    if (k & KEY_A) {
      if (files[chosen][0] == '/')
        snprintf(cfg.rom_path, sizeof(cfg.rom_path), "%s", files[chosen]);
      else
        snprintf(cfg.rom_path, sizeof(cfg.rom_path), "%s%s", dir,
                 files[chosen]);
      config_save();
      printf("\nSelected: %s\n", cfg.rom_path);
      return 1;
    }

    if (k & KEY_B)
      return 0;
  }
}

static void dump_header(const nds_header_t *h) {
  printf("Title:    %.12s\n", h->title);
  printf("ARM9: off=0x%08lX load=0x%08lX\n", (unsigned long)h->arm9_offset,
         (unsigned long)h->arm9_load);
  printf("      entry=0x%08lX size=%lu\n", (unsigned long)h->arm9_entry,
         (unsigned long)h->arm9_size);
  printf("ARM7: off=0x%08lX load=0x%08lX\n", (unsigned long)h->arm7_offset,
         (unsigned long)h->arm7_load);
  printf("      entry=0x%08lX size=%lu\n", (unsigned long)h->arm7_entry,
         (unsigned long)h->arm7_size);
}

// ARM7 bootloader binary, embedded via load_bin.s (GPL-2.0, Chishm/WinterMute)
extern const uint8_t load_bin[];
extern const uint32_t load_bin_size;

// bootloader parameter offsets — must match load_crt0.s header layout
#define BL_CLUSTER_OFF 4
#define BL_INITDISC_OFF 8
#define BL_DLDI_OFF 12
#define BL_ARGSTART_OFF 16
#define BL_ARGSIZE_OFF 20
#define BL_DSISD_OFF 28
#define BL_DSIMODE_OFF 32

// VRAM bank C in LCDC mode: bootloader goes at offset 0x8000 within the bank.
// ARM7 sees this at 0x06008000 when VRAM C is mapped to ARM7.
#define LCDC_BANK_C_BL ((uint8_t *)0x06848000)
#define BL_MAX_SIZE 0x18000

// save ROM to SD then launch it via ARM7 bootloader in VRAM.
// `filename` is the sd:/ path passed as argv[0] so NitroFS can find itself.
// does not return.
__attribute__((noreturn)) static void launch_nds(uint32_t cluster,
                                                 const char *filename) {
  irqDisable(IRQ_ALL);

  // map VRAM bank C for direct ARM9 write access
  VRAM_C_CR = VRAM_ENABLE | VRAM_C_LCD;

  // copy bootloader into upper 96K of VRAM C
  memcpy(LCDC_BANK_C_BL, load_bin, load_bin_size);
  memset(LCDC_BANK_C_BL + load_bin_size, 0, BL_MAX_SIZE - load_bin_size);

  // write bootloader parameters into the header
  volatile uint32_t *p = (volatile uint32_t *)LCDC_BANK_C_BL;
  p[BL_CLUSTER_OFF / 4] = cluster; // FAT start cluster of NDS file
  p[BL_INITDISC_OFF / 4] = 1;      // re-initialize SD
  p[BL_DLDI_OFF / 4] = 0;          // no DLDI patching needed (DSi SD)
  p[BL_DSISD_OFF / 4] = 1;         // using DSi internal SD
  p[BL_DSIMODE_OFF / 4] = 1;       // DSi mode

  // write argv[0] = filename into the bootloader's argument area.
  // argStart initially holds the bootloader size (offset to arg area).
  uint32_t arg_off = p[BL_ARGSTART_OFF / 4];
  arg_off = (arg_off + 3) & ~3u; // word-align
  uint8_t *arg_dst = LCDC_BANK_C_BL + arg_off;
  size_t fn_len = strlen(filename) + 1; // include null terminator
  memcpy(arg_dst, filename, fn_len);
  p[BL_ARGSTART_OFF / 4] = arg_off;
  p[BL_ARGSIZE_OFF / 4] = (uint32_t)fn_len;

  // hand VRAM bank C to ARM7 (maps at 0x06000000)
  VRAM_C_CR = VRAM_ENABLE | VRAM_C_ARM7_0x06000000;

  // give ROM/card bus to ARM7
  REG_EXMEMCNT |= ARM7_OWNS_ROM | ARM7_OWNS_CARD;

  // set up ARM9 passme loop
  *((vu32 *)0x02FFFFFC) = 0;
  *((vu32 *)0x02FFFE04) = (u32)0xE59FF018;
  *((vu32 *)0x02FFFE24) = (u32)0x02FFFE04;

  // reset ARM7 to bootloader entry in VRAM
  resetARM7(0x06008000);

  // ARM9 soft reset → enters passme loop; ARM7 bootloader loads and starts
  swiSoftReset();
  while (1) {
  }
}

int main(void) {
  defaultExceptionHandler();

  videoSetMode(MODE_0_2D);
  videoSetModeSub(MODE_0_2D);
  vramSetBankA(VRAM_A_MAIN_BG);
  vramSetBankC(VRAM_C_SUB_BG);

  consoleInit(&topScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true,
              true);
  consoleInit(&bottomScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 3, false,
              true);

  consoleSelect(&topScreen);

  fat_ok = fatInitDefault();
  config_load();

  printf("Initializing WiFi...\n");
  if (!Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE)) {
    printf("WiFi init failed!\n");
    return 1;
  }
  Wifi_DisableWifi();
  swiWaitForVBlank();

  while (1) {
    consoleClear();
    consoleSelect(&topScreen);
    consoleClear();

    printf("=== ndsfetch ===\n\n");
    printf("Server: %s:%s\n", cfg.server_ip, cfg.server_port);
    printf("ROM:    %s\n", cfg.rom_path);
    if (cfg.wifi_ssid[0])
      printf("WiFi:   %s\n", cfg.wifi_ssid);
    printf("\n");
    printf("A: Download + run\n");
    printf("X: Browse server files\n");
    printf("Y: Edit settings\n");
    printf("START: Reboot\n\n");

    int action = 0;
    while (!action) {
      swiWaitForVBlank();
      scanKeys();
      u16 k = keysDown();

      if (k & KEY_A) {
        action = 1;
        break;
      }

      if (k & KEY_X) {
        action = 3;
        break;
      }

      if (k & KEY_Y) {
        action = 2;
        consoleSelect(&topScreen);
        consoleClear();
        printf("=== Settings ===\n\n");
        printf("1/UP:   Server IP\n");
        printf("2/DOWN: Server port\n");
        printf("3/LEFT: ROM path\n");
        printf("4/RIGHT: Clear saved WiFi\n");
        printf("B: Back\n\n");

        while (1) {
          swiWaitForVBlank();
          scanKeys();
          u16 sk = keysDown();

          if (sk & KEY_UP) {
            kbd_input("Server IP:", cfg.server_ip, (int)sizeof(cfg.server_ip));
            config_save();
            break;
          }
          if (sk & KEY_DOWN) {
            kbd_input("Server Port:", cfg.server_port,
                      (int)sizeof(cfg.server_port));
            config_save();
            break;
          }
          if (sk & KEY_LEFT) {
            kbd_input("ROM Path:", cfg.rom_path, (int)sizeof(cfg.rom_path));
            config_save();
            break;
          }
          if (sk & KEY_RIGHT) {
            cfg.wifi_ssid[0] = '\0';
            cfg.wifi_pass[0] = '\0';
            config_save();
            printf("WiFi credentials cleared.\n");
            swiWaitForVBlank();
            swiWaitForVBlank();
            break;
          }
          if (sk & KEY_B)
            break;
        }
        break;
      }

      if (k & KEY_START) {
        swiSoftReset();
      }
    }

    if (action != 1 && action != 3)
      continue;

    consoleSelect(&bottomScreen);
    consoleClear();

    if (!wifi_connect()) {
      wait_key("WiFi failed.");
      Wifi_DisableWifi();
      swiWaitForVBlank();
      continue;
    }

    printf("\n");

    if (action == 3) {
      consoleSelect(&topScreen);
      if (!browse_server()) {
        Wifi_DisableWifi();
        swiWaitForVBlank();
        continue;
      }
      consoleSelect(&bottomScreen);
    }

    if (!http_get(cfg.server_ip, cfg.server_port, cfg.rom_path)) {
      wait_key("Download failed.");
      Wifi_DisableWifi();
      swiWaitForVBlank();
      continue;
    }

    Wifi_DisableWifi();
    swiWaitForVBlank();
    swiWaitForVBlank();

    consoleSelect(&topScreen);
    consoleClear();

    nds_header_t *hdr = (nds_header_t *)rom_buf;
    printf("=== ROM Downloaded ===\n\n");
    dump_header(hdr);

    // xxtract filename from rom_path for save label
    const char *fname = strrchr(cfg.rom_path, '/');
    fname = fname ? fname + 1 : cfg.rom_path;

    printf("\nA: Save to SD + launch\n");
    printf("SELECT: Save %s to SD only\n", fname);
    printf("B: Discard + retry\n");

    int done = 0;
    while (!done) {
      swiWaitForVBlank();
      scanKeys();
      u16 keys = keysDown();

      if ((keys & KEY_A) || (keys & KEY_SELECT)) {
        int launch = (keys & KEY_A) != 0;
        printf("\nSaving to SD...\n");
        if (!fat_ok) {
          printf("FAT not available!\n");
          continue;
        }

        char sd_path[160];
        char fat_path[160];
        snprintf(sd_path, sizeof(sd_path), "sd:/%s", fname);
        snprintf(fat_path, sizeof(fat_path), "fat:/%s", fname);
        const char *paths[] = {sd_path, fat_path};
        FILE *f = nil;
        const char *used = nil;
        for (int p = 0; p < 2 && !f; p++) {
          f = fopen(paths[p], "wb");
          if (f)
            used = paths[p];
        }

        if (!f) {
          printf("fopen failed: %s (%d)\n", strerror(errno), errno);
          continue;
        }
        size_t w = fwrite(rom_buf, 1, (size_t)rom_size, f);
        fclose(f);
        printf("Wrote %zu bytes to %s\n", w, used);

        if (launch) {
          // get the FAT start cluster via stat()
          struct stat st;
          if (stat(used, &st) < 0) {
            printf("stat failed: %s\n", strerror(errno));
            wait_key("Cannot get cluster. Reboot to launch.");
            done = 1;
            continue;
          }
          uint32_t cluster = (uint32_t)st.st_ino;
          printf("Cluster: %lu\n", (unsigned long)cluster);
          printf("Launching...\n");
          swiWaitForVBlank();

          free(rom_buf);
          rom_buf = nil;
          rom_size = 0;

          launch_nds(cluster, used);
        }

        wait_key("Saved! Reboot to launch.");
        done = 1;
      }

      if (keys & KEY_B) {
        done = 1;
      }
    }

    free(rom_buf);
    rom_buf = nil;
    rom_size = 0;
  }

  return 0;
}
