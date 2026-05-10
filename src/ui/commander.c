/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ui/commander.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <ncurses.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "common/protocol.h"
#include "net/file_transfer.h"
#include "net/hostlist.h"
#include "net/raw_socket.h"
#include "net/remote_dir.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define MAX_LOCAL_ENTRIES 512
#define STATUS_LEN 160

enum FocusPane {
  FOCUS_REMOTE = 0,
  FOCUS_LOCAL = 1,
};

struct LocalEntry {
  char name[NAME_MAX + 1];
  off_t size;
  mode_t mode;
  int is_dir;
};

struct LocalPanel {
  char cwd[PATH_MAX];
  struct LocalEntry entries[MAX_LOCAL_ENTRIES];
  int count;
  int selected;
  int scroll;
};

struct RemotePanel {
  struct RemoteDirEntry entries[REMOTE_DIR_MAX_ENTRIES];
  int count;
  int selected;
  int scroll;
  int loaded;
};

struct AppState {
  struct RawSocket sock;
  struct RemoteHost *active_host;
  struct LocalPanel local;
  struct RemotePanel remote;
  enum FocusPane focus;
  char remote_path[RMTDOS_PATH_BYTES];
  char status[STATUS_LEN];
  int running;
};

static void set_status(struct AppState *app, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(app->status, sizeof(app->status), fmt, ap);
  va_end(ap);
}

static int is_exit_key(int ch) { return ch == 'q' || ch == 27 || ch == 29; }

static int join_path(char *out, size_t out_len, const char *dir,
                     const char *name) {
  int n = snprintf(out, out_len, "%s/%s", dir, name);
  return n < 0 || (size_t)n >= out_len ? -1 : 0;
}

static int local_entry_cmp(const void *lhs, const void *rhs) {
  const struct LocalEntry *a = lhs;
  const struct LocalEntry *b = rhs;

  if (!strcmp(a->name, "..")) {
    return -1;
  }
  if (!strcmp(b->name, "..")) {
    return 1;
  }
  if (a->is_dir != b->is_dir) {
    return b->is_dir - a->is_dir;
  }
  return strcasecmp(a->name, b->name);
}

static int local_panel_load(struct LocalPanel *panel, const char *path) {
  DIR *dir;
  struct dirent *de;
  char resolved[PATH_MAX];

  if (!realpath(path, resolved)) {
    return -1;
  }

  dir = opendir(resolved);
  if (!dir) {
    return -1;
  }

  snprintf(panel->cwd, sizeof(panel->cwd), "%s", resolved);
  panel->count = 0;
  panel->selected = 0;
  panel->scroll = 0;

  if (strcmp(panel->cwd, "/") && panel->count < MAX_LOCAL_ENTRIES) {
    struct LocalEntry *entry = &panel->entries[panel->count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "..");
    entry->is_dir = 1;
    entry->mode = S_IFDIR;
  }

  while ((de = readdir(dir)) && panel->count < MAX_LOCAL_ENTRIES) {
    struct LocalEntry *entry;
    char full[PATH_MAX];
    struct stat st;

    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }

    entry = &panel->entries[panel->count];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", de->d_name);
    if (join_path(full, sizeof(full), panel->cwd, de->d_name)) {
      continue;
    }

    if (!lstat(full, &st)) {
      entry->size = st.st_size;
      entry->mode = st.st_mode;
      entry->is_dir = S_ISDIR(st.st_mode);
    }

    ++panel->count;
  }

  closedir(dir);
  qsort(panel->entries, panel->count, sizeof(panel->entries[0]),
        local_entry_cmp);
  return 0;
}

static void local_panel_enter(struct AppState *app) {
  struct LocalEntry *entry;
  char next[PATH_MAX];

  if (app->local.count <= 0) {
    return;
  }

  entry = &app->local.entries[app->local.selected];
  if (!entry->is_dir) {
    set_status(app, "Local file selected: %s", entry->name);
    return;
  }

  if (join_path(next, sizeof(next), app->local.cwd, entry->name)) {
    set_status(app, "Path is too long.");
    return;
  }
  if (local_panel_load(&app->local, next)) {
    set_status(app, "Cannot enter %s: %s", entry->name, strerror(errno));
  } else {
    set_status(app, "Local: %s", app->local.cwd);
  }
}

static int remote_path_join(char *out, size_t out_len, const char *dir,
                            const char *name) {
  size_t len;
  int n;

  if (!strcmp(name, ".") || !name[0]) {
    n = snprintf(out, out_len, "%s", dir);
  } else if (!strcmp(name, "..")) {
    char tmp[RMTDOS_PATH_BYTES];
    char *slash;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (len > 3 && tmp[len - 1] == '\\') {
      tmp[len - 1] = '\0';
    }
    slash = strrchr(tmp, '\\');
    if (slash && slash > tmp + 2) {
      slash[0] = '\0';
    } else if (slash) {
      slash[1] = '\0';
    }
    n = snprintf(out, out_len, "%s", tmp);
  } else {
    len = strlen(dir);
    n = snprintf(out, out_len, "%s%s%s", dir,
                 len && dir[len - 1] == '\\' ? "" : "\\", name);
  }

  return n < 0 || (size_t)n >= out_len ? -1 : 0;
}

static int remote_panel_load(struct AppState *app) {
  if (remote_dir_fetch(&app->sock, app->active_host->if_addr, app->remote_path,
                       app->remote.entries, REMOTE_DIR_MAX_ENTRIES,
                       &app->remote.count)) {
    app->remote.loaded = 0;
    set_status(app, "Remote listing failed for %s", app->remote_path);
    return -1;
  }

  app->remote.selected = 0;
  app->remote.scroll = 0;
  app->remote.loaded = 1;
  set_status(app, "Remote: %s", app->remote_path);
  return 0;
}

static void remote_panel_enter(struct AppState *app) {
  struct RemoteDirEntry *entry;
  char next[RMTDOS_PATH_BYTES];

  if (!app->remote.loaded || app->remote.count <= 0) {
    set_status(app, "Remote directory is empty or not loaded.");
    return;
  }

  entry = &app->remote.entries[app->remote.selected];
  if (!entry->is_dir) {
    set_status(app, "Remote file selected: %s", entry->name);
    return;
  }

  if (remote_path_join(next, sizeof(next), app->remote_path, entry->name)) {
    set_status(app, "Remote path is too long.");
    return;
  }

  snprintf(app->remote_path, sizeof(app->remote_path), "%s", next);
  remote_panel_load(app);
}

static void draw_status_bar(struct AppState *app) {
  int rows;
  int cols;

  getmaxyx(stdscr, rows, cols);
  attron(COLOR_PAIR(3));
  mvhline(rows - 2, 0, ' ', cols);
  mvprintw(rows - 2, 1, "%.*s", cols - 2, app->status);
  mvhline(rows - 1, 0, ' ', cols);
  mvprintw(rows - 1, 1,
           "Tab switch  Enter open  u upload  d download  r refresh  q quit");
  attroff(COLOR_PAIR(3));
}

static void draw_box_title(WINDOW *win, const char *title, int focused) {
  if (focused) {
    wattron(win, COLOR_PAIR(2) | A_BOLD);
  }
  box(win, 0, 0);
  mvwprintw(win, 0, 2, " %s ", title);
  if (focused) {
    wattroff(win, COLOR_PAIR(2) | A_BOLD);
  }
}

static void draw_remote_panel(struct AppState *app, WINDOW *win) {
  char mac[MAC_ADDR_FMT_LEN];
  int rows;
  int cols;
  int list_rows;
  int i;

  getmaxyx(win, rows, cols);
  list_rows = rows - 4;
  draw_box_title(win, "Remote DOS", app->focus == FOCUS_REMOTE);

  mvwprintw(win, 1, 2, "Host: %s",
            fmt_mac_addr(mac, sizeof(mac), app->active_host->if_addr));
  mvwprintw(win, 2, 2, "%.*s", cols - 4, app->remote_path);

  wattron(win, A_BOLD);
  mvwprintw(win, 3, 2, "%-*s %10s", cols - 16, "Name", "Size");
  wattroff(win, A_BOLD);

  if (!app->remote.loaded) {
    wattron(win, A_DIM);
    mvwprintw(win, 5, 2, "Remote listing not loaded.");
    wattroff(win, A_DIM);
    return;
  }

  if (app->remote.selected < app->remote.scroll) {
    app->remote.scroll = app->remote.selected;
  } else if (app->remote.selected >= app->remote.scroll + list_rows) {
    app->remote.scroll = app->remote.selected - list_rows + 1;
  }

  for (i = 0; i < list_rows && app->remote.scroll + i < app->remote.count;
       ++i) {
    int idx = app->remote.scroll + i;
    struct RemoteDirEntry *entry = &app->remote.entries[idx];
    int y = 4 + i;
    char display[RMTDOS_DIR_ENTRY_NAME_BYTES + 4];

    snprintf(display, sizeof(display), "%s%s", entry->name,
             entry->is_dir ? "\\" : "");

    if (idx == app->remote.selected && app->focus == FOCUS_REMOTE) {
      wattron(win, COLOR_PAIR(1));
    }

    mvwprintw(win, y, 2, "%-*.*s", cols - 16, cols - 16, display);
    if (!entry->is_dir) {
      mvwprintw(win, y, cols - 13, "%10lu", (unsigned long)entry->size);
    } else {
      mvwprintw(win, y, cols - 13, "%10s", "<DIR>");
    }

    if (idx == app->remote.selected && app->focus == FOCUS_REMOTE) {
      wattroff(win, COLOR_PAIR(1));
    }
  }
}

static void draw_local_panel(struct AppState *app, WINDOW *win) {
  struct LocalPanel *panel = &app->local;
  int rows;
  int cols;
  int list_rows;
  int i;

  getmaxyx(win, rows, cols);
  list_rows = rows - 4;
  draw_box_title(win, "Local Linux", app->focus == FOCUS_LOCAL);
  mvwprintw(win, 1, 2, "%.*s", cols - 4, panel->cwd);

  wattron(win, A_BOLD);
  mvwprintw(win, 2, 2, "%-*s %10s", cols - 16, "Name", "Size");
  wattroff(win, A_BOLD);

  if (panel->selected < panel->scroll) {
    panel->scroll = panel->selected;
  } else if (panel->selected >= panel->scroll + list_rows) {
    panel->scroll = panel->selected - list_rows + 1;
  }

  for (i = 0; i < list_rows && panel->scroll + i < panel->count; ++i) {
    int idx = panel->scroll + i;
    struct LocalEntry *entry = &panel->entries[idx];
    int y = 3 + i;
    char display[NAME_MAX + 4];

    snprintf(display, sizeof(display), "%s%s", entry->name,
             entry->is_dir ? "/" : "");

    if (idx == panel->selected && app->focus == FOCUS_LOCAL) {
      wattron(win, COLOR_PAIR(1));
    }

    mvwprintw(win, y, 2, "%-*.*s", cols - 16, cols - 16, display);
    if (!entry->is_dir) {
      mvwprintw(win, y, cols - 13, "%10ld", (long)entry->size);
    } else {
      mvwprintw(win, y, cols - 13, "%10s", "<DIR>");
    }

    if (idx == panel->selected && app->focus == FOCUS_LOCAL) {
      wattroff(win, COLOR_PAIR(1));
    }
  }
}

static void draw_commander(struct AppState *app) {
  int rows;
  int cols;
  int pane_h;
  int left_w;
  WINDOW *left;
  WINDOW *right;

  erase();
  getmaxyx(stdscr, rows, cols);
  pane_h = rows - 2;
  left_w = cols / 2;

  left = newwin(pane_h, left_w, 0, 0);
  right = newwin(pane_h, cols - left_w, 0, left_w);

  draw_remote_panel(app, left);
  draw_local_panel(app, right);
  draw_status_bar(app);

  wnoutrefresh(stdscr);
  wnoutrefresh(left);
  wnoutrefresh(right);
  doupdate();

  delwin(left);
  delwin(right);
}

static void process_incoming_packet(struct AppState *app) {
  uint8_t buf[ETH_FRAME_LEN];
  ssize_t received;
  const struct ether_header *eh;
  const struct ProtocolHeader *ph;

  received = recv(app->sock.sock_fd, buf, sizeof(buf), 0);
  if (received < (ssize_t)(sizeof(*eh) + sizeof(*ph))) {
    return;
  }

  eh = (const struct ether_header *)buf;
  ph = (const struct ProtocolHeader *)(eh + 1);

  if (memcmp(eh->ether_dhost, app->sock.if_addr, ETH_ALEN) ||
      ntohl(ph->signature) != PACKET_SIGNATURE ||
      ntohl(ph->session_id) != app->sock.session_id) {
    return;
  }

  if (ntohs(ph->pkt_type) == V1_STATUS_RESP) {
    hostlist_register(buf, received);
  }
}

static int socket_has_data(int fd, int timeout_ms) {
  fd_set fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  return select(fd + 1, &fds, NULL, NULL, &tv) > 0;
}

static void draw_selector(struct AppState *app) {
  int rows;
  int cols;
  int y = 1;
  int iter = 0;
  struct RemoteHost *host;
  struct timeval now;

  erase();
  getmaxyx(stdscr, rows, cols);
  box(stdscr, 0, 0);
  mvprintw(0, 2, " rmtdos LAN server selector ");
  mvprintw(y++, 2, "%s", RMTDOS_FC_VERSION);
  mvprintw(y++, 2, "Interface: %s  EtherType: %04x", app->sock.if_name,
           app->sock.ethertype);
  y++;

  attron(COLOR_PAIR(2) | A_BOLD);
  mvprintw(y++, 2, "Id  MAC address        Mode   Size    Last seen");
  attroff(COLOR_PAIR(2) | A_BOLD);

  gettimeofday(&now, NULL);
  while ((host = hostlist_iter(&iter)) && y < rows - 3) {
    struct timeval diff;
    char mac[MAC_ADDR_FMT_LEN];

    timersub(&now, &host->tv_last_resp, &diff);
    mvprintw(y++, 2, "%2d  %s  %4d   %3dx%-3d %ld.%03lds", host->index,
             fmt_mac_addr(mac, sizeof(mac), host->if_addr),
             host->status.video_mode, host->status.text_cols,
             host->status.text_rows, diff.tv_sec, diff.tv_usec / 1000);
  }

  mvprintw(rows - 2, 2, "Press 0-9 to select. q/Esc/Ctrl-] exits.");
  refresh();
  (void)cols;
}

static struct RemoteHost *run_selector(struct AppState *app) {
  struct timeval last_probe = {0};

  set_status(app, "Probing for rmtdos servers...");

  while (app->running) {
    struct timeval now;
    struct timeval diff;
    int ch;

    gettimeofday(&now, NULL);
    timersub(&now, &last_probe, &diff);
    if (!timerisset(&last_probe) || diff.tv_sec >= 2) {
      send_status_req(&app->sock, NULL);
      last_probe = now;
    }

    while (socket_has_data(app->sock.sock_fd, 20)) {
      process_incoming_packet(app);
    }

    draw_selector(app);

    ch = getch();
    if (ch == ERR) {
      continue;
    }
    if (is_exit_key(ch)) {
      app->running = 0;
      return NULL;
    }
    if (ch >= '0' && ch <= '9') {
      struct RemoteHost *host = hostlist_find_by_index(ch - '0');
      if (host) {
        return host;
      }
    }
  }

  return NULL;
}

static void shell_transfer_pause(void) {
  printf("\nPress Enter to return to rmtdos-file-commander...");
  fflush(stdout);
  while (getchar() != '\n') {
  }
}

static void ui_reset(void) {
  clear();
  refresh();
}

static int prompt_text(const char *label, char *buf, size_t len) {
  echo();
  curs_set(1);
  mvprintw(LINES - 2, 1, "%s", label);
  clrtoeol();
  move(LINES - 2, (int)strlen(label) + 1);
  if (getnstr(buf, (int)len - 1) == ERR) {
    noecho();
    curs_set(0);
    return -1;
  }
  noecho();
  curs_set(0);
  return buf[0] ? 0 : -1;
}

static void upload_selected(struct AppState *app) {
  struct LocalEntry *entry;
  char local_path[PATH_MAX];
  char remote_name[RMTDOS_PATH_BYTES];
  char remote_path[FILE_TRANSFER_NAME_BYTES];
  int rc;

  if (app->local.count <= 0) {
    return;
  }

  entry = &app->local.entries[app->local.selected];
  if (entry->is_dir) {
    set_status(app, "Select a local file before uploading.");
    return;
  }

  if (strlen(entry->name) >= sizeof(remote_name)) {
    set_status(app, "Remote filename is too long.");
    return;
  }
  strcpy(remote_name, entry->name);
  if (prompt_text("Remote DOS filename: ", remote_name, sizeof(remote_name))) {
    set_status(app, "Upload cancelled.");
    return;
  }

  if (remote_path_join(remote_path, sizeof(remote_path), app->remote_path,
                       remote_name)) {
    set_status(app, "Remote transfer path must fit in %d characters.",
               FILE_TRANSFER_NAME_BYTES - 1);
    return;
  }

  if (join_path(local_path, sizeof(local_path), app->local.cwd, entry->name)) {
    set_status(app, "Local path is too long.");
    return;
  }

  def_prog_mode();
  endwin();
  rc = file_transfer_put(&app->sock, app->active_host->if_addr, local_path,
                         remote_path);
  shell_transfer_pause();
  reset_prog_mode();
  ui_reset();

  if (!rc) {
    remote_panel_load(app);
  }
  set_status(app, rc ? "Upload failed." : "Upload complete.");
}

static void download_prompted(struct AppState *app) {
  char remote_name[RMTDOS_PATH_BYTES];
  char remote_path[FILE_TRANSFER_NAME_BYTES];
  char local_name[NAME_MAX + 1];
  char local_path[PATH_MAX];
  int rc;

  memset(remote_name, 0, sizeof(remote_name));
  if (app->focus == FOCUS_REMOTE && app->remote.loaded &&
      app->remote.count > 0 &&
      !app->remote.entries[app->remote.selected].is_dir) {
    snprintf(remote_name, sizeof(remote_name), "%s",
             app->remote.entries[app->remote.selected].name);
  }

  if (prompt_text("Remote DOS filename: ", remote_name, sizeof(remote_name))) {
    set_status(app, "Download cancelled.");
    return;
  }
  snprintf(local_name, sizeof(local_name), "%s", remote_name);
  if (prompt_text("Local filename: ", local_name, sizeof(local_name))) {
    set_status(app, "Download cancelled.");
    return;
  }

  if (join_path(local_path, sizeof(local_path), app->local.cwd, local_name)) {
    set_status(app, "Local path is too long.");
    return;
  }

  if (remote_path_join(remote_path, sizeof(remote_path), app->remote_path,
                       remote_name)) {
    set_status(app, "Remote transfer path must fit in %d characters.",
               FILE_TRANSFER_NAME_BYTES - 1);
    return;
  }

  def_prog_mode();
  endwin();
  rc = file_transfer_get(&app->sock, app->active_host->if_addr, remote_path,
                         local_path);
  shell_transfer_pause();
  reset_prog_mode();
  ui_reset();

  if (!rc) {
    local_panel_load(&app->local, app->local.cwd);
  }
  set_status(app, rc ? "Download failed." : "Download complete.");
}

static void process_commander_key(struct AppState *app, int ch) {
  struct LocalPanel *local = &app->local;
  struct RemotePanel *remote = &app->remote;

  if (is_exit_key(ch)) {
    app->running = 0;
    return;
  }

  switch (ch) {
    case '\t':
      app->focus = app->focus == FOCUS_REMOTE ? FOCUS_LOCAL : FOCUS_REMOTE;
      break;
    case KEY_UP:
      if (app->focus == FOCUS_LOCAL && local->selected > 0) {
        --local->selected;
      } else if (app->focus == FOCUS_REMOTE && remote->selected > 0) {
        --remote->selected;
      }
      break;
    case KEY_DOWN:
      if (app->focus == FOCUS_LOCAL && local->selected < local->count - 1) {
        ++local->selected;
      } else if (app->focus == FOCUS_REMOTE &&
                 remote->selected < remote->count - 1) {
        ++remote->selected;
      }
      break;
    case '\n':
    case KEY_ENTER:
      if (app->focus == FOCUS_LOCAL) {
        local_panel_enter(app);
      } else {
        remote_panel_enter(app);
      }
      break;
    case 'u':
    case 'U':
    case KEY_F(5):
      if (app->focus != FOCUS_LOCAL) {
        set_status(app, "Switch to the local pane to upload a selected file.");
      } else {
        upload_selected(app);
      }
      break;
    case 'd':
    case 'D':
      download_prompted(app);
      break;
    case 'r':
    case 'R':
      if (app->focus == FOCUS_REMOTE) {
        remote_panel_load(app);
      } else if (local_panel_load(local, local->cwd)) {
        set_status(app, "Refresh failed: %s", strerror(errno));
      } else {
        set_status(app, "Refreshed %s", local->cwd);
      }
      break;
  }
}

static void run_commander(struct AppState *app) {
  struct timeval last_session = {0};
  set_status(app, "Connected. Loading remote DOS directory...");
  remote_panel_load(app);

  while (app->running) {
    struct timeval now;
    struct timeval diff;
    int ch;

    gettimeofday(&now, NULL);
    timersub(&now, &last_session, &diff);
    if (!timerisset(&last_session) || diff.tv_sec >= 2) {
      send_session_start(&app->sock, app->active_host->if_addr);
      last_session = now;
    }

    while (socket_has_data(app->sock.sock_fd, 1)) {
      process_incoming_packet(app);
    }

    draw_commander(app);
    ch = getch();
    if (ch != ERR) {
      process_commander_key(app, ch);
    }
  }
}

int commander_run(const struct CommanderConfig *config) {
  struct AppState app;
  int rc = 1;

  memset(&app, 0, sizeof(app));
  app.running = 1;
  app.focus = FOCUS_LOCAL;
  snprintf(app.remote_path, sizeof(app.remote_path), "C:\\");
  set_status(&app, "Starting...");

  if (create_socket(&app.sock, config->if_name, config->ethertype) < 0) {
    return 1;
  }

  hostlist_create();

  if (local_panel_load(&app.local, ".")) {
    perror("local directory");
    goto out_socket;
  }

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  curs_set(0);
  start_color();
  use_default_colors();
  init_pair(1, COLOR_BLACK, COLOR_CYAN);
  init_pair(2, COLOR_CYAN, -1);
  init_pair(3, COLOR_BLACK, COLOR_WHITE);

  app.active_host = run_selector(&app);
  if (app.active_host) {
    run_commander(&app);
  }

  endwin();
  rc = 0;

out_socket:
  hostlist_destroy();
  close_socket(&app.sock);
  return rc;
}
