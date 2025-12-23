#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "lib/raylib.h"
#include "lib/tmt.h"
#include "lib/tmt.c"
#include "lib/go_mono_ttf.h"

const int fontHeight = 26;
const int fontWidth = 13;
const int spacing = 0;
int shouldExit = 0;
TMT *vt;
int master_fd, slave_fd;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void die(char*);
int max(int a, int b);
void resize();
void callback(tmt_msg_t m, TMT *vt, const void *a, void *p);
void* start_shell(void* param);

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1024, 768, "term");
    SetWindowMinSize(400, 400);
    SetExitKey(0);
    Vector2 dpi_scale = GetWindowScaleDPI();
    int font_load_size = (int)(fontHeight * dpi_scale.y);
    Font font = LoadFontFromMemory(".ttf", lib_Go_Mono_ttf, lib_Go_Mono_ttf_len, font_load_size, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    SetTargetFPS(60);

    Color colorFg = GetColor(0x222222FF);
    Color colorBg = GetColor(0xFFFFFFFF);
    Color colors[10] = {
      GetColor(0x1D1F21FF), // default/black
      GetColor(0x1D1F21FF), // black
      GetColor(0xCC6666FF), // red
      GetColor(0xB5BD68FF), // green
      GetColor(0xF0C674FF), // yellow
      GetColor(0x81A2BEFF), // blue
      GetColor(0xB294BBFF), // magenta
      GetColor(0x8ABEB7FF), // cyan
      GetColor(0xC5C8C6FF), // white
      GetColor(0xFFFFFFFF), // pure white
    };

    setenv("TERM", "linux", 1);
    vt = tmt_open(80, 24, callback, NULL, NULL);
    if (!vt) die("can not open virtual terminal");
    resize();

    pthread_t thread;
    pthread_create(&thread, NULL, start_shell, NULL);

    while (!WindowShouldClose() && !shouldExit) {
      if (IsWindowResized()) {
        resize();
      }
      char k;
      pthread_mutex_lock(&mutex);
      if (IsKeyDown(KEY_LEFT_CONTROL)) {
        for (int i = 65; i <= 79; i++) {
          if (IsKeyPressed(i)) {
            char c = i-64;
            write(master_fd, &c, 1);
          }
        }
      }
      while ((k = GetCharPressed())) {
        write(master_fd, &k, 1);
      }
      #define PRESSED_OR_REPEAT(x) IsKeyPressed((x)) || IsKeyPressedRepeat((x))
      if (IsKeyPressed(KEY_TAB)) write(master_fd, "\t", 1); 
      if (PRESSED_OR_REPEAT(KEY_ENTER)) write(master_fd, "\r", 1); 
      if (PRESSED_OR_REPEAT(KEY_ESCAPE)) write(master_fd, TMT_KEY_ESCAPE, 1); 
      if (PRESSED_OR_REPEAT(KEY_BACKSPACE)) write(master_fd, TMT_KEY_BACKSPACE, 1); 
      if (PRESSED_OR_REPEAT(KEY_LEFT)) write(master_fd, TMT_KEY_LEFT, 3); 
      if (PRESSED_OR_REPEAT(KEY_DOWN)) write(master_fd, TMT_KEY_DOWN, 3); 
      if (PRESSED_OR_REPEAT(KEY_UP)) write(master_fd, TMT_KEY_UP, 3); 
      if (PRESSED_OR_REPEAT(KEY_RIGHT)) write(master_fd, TMT_KEY_RIGHT, 3); 
      if (IsKeyPressed(KEY_V) && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyDown(KEY_LEFT_SHIFT)) {
        char *clip = GetClipboardText();
        write(master_fd, clip, strlen(clip)); 
      }
      pthread_mutex_unlock(&mutex);

      BeginDrawing();
      ClearBackground(colorBg);
      const TMTSCREEN *s = tmt_screen(vt);
      const TMTPOINT *cu = tmt_cursor(vt);
      char ch[2]; ch[1] = '\0';
      for (size_t r = 0; r < s->nline; r++) {
        for (size_t c = 0; c < s->ncol; c++) {
          TMTCHAR tmc = s->lines[r]->chars[c];
          ch[0] = tmc.c;
          Vector2 p = {4+c*fontWidth, 4+r*fontHeight};
          if (cu->c==c && cu->r==r) {
            DrawRectangle(p.x, p.y, fontWidth, fontHeight, colorFg);
            DrawTextEx(font, &ch[0], p, fontHeight, spacing, colorBg);
          } else {
            if (tmc.a.reverse) { tmc.a.bg = 1; tmc.a.fg = 9; }
            if (tmc.a.bg > 0) DrawRectangle(p.x, p.y, fontWidth, fontHeight, colors[max(0, tmc.a.bg)]);
            DrawTextEx(font, &ch[0], p, fontHeight, spacing, colors[max(0, tmc.a.fg)]);
          }
        }
      }
      EndDrawing();
    }
    CloseWindow();
    tmt_close(vt);
    pthread_mutex_destroy(&mutex);
    return 0;
}

void die(char *e) {
  printf("error: %s\n", e);
  exit(1);
};

int max(int a, int b) {
  if (a > b) {
    return a;
  }
  return b;
}

Vector2 resize_size() {
  Vector2 dpi = GetWindowScaleDPI();
  int rows = (GetRenderHeight()/dpi.y-8)/fontHeight;
  int cols = (GetRenderWidth()/dpi.x-8)/fontWidth;
  return (Vector2){cols, rows};
}

void resize() {
  Vector2 size = resize_size();
  tmt_resize(vt, size.y, size.x);
  struct winsize ws = { .ws_row = size.y, .ws_col = size.x };
  if (ioctl(master_fd, TIOCSWINSZ, &ws) == -1) {
    perror("ioctl TIOCSWINSZ failed");
  }
}

void callback(tmt_msg_t m, TMT *vt, const void *a, void *p) {
  switch (m) {
    case TMT_MSG_BELL:
      printf("bell\n");
      break;
    case TMT_MSG_UPDATE:
      tmt_clean(vt);
      break;
    case TMT_MSG_ANSWER:
      //pthread_mutex_lock(&mutex);
      //write(master_fd, (const char *)a, strlen((const char *)a));
      //pthread_mutex_unlock(&mutex);
      break;
    case TMT_MSG_MOVED:
      tmt_clean(vt);
      break;
  }
}

void* start_shell(void* param) {
  pid_t pid;
  char buffer[2048];
  Vector2 size = resize_size();
  struct winsize ws = { .ws_row = size.y, .ws_col = size.x };
  if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) < 0) {
    perror("openpty failed");
    die("fatal");
  }
  pid = fork();
  if (pid < 0) {
    perror("fork failed");
    die("fatal");
  } else if (pid == 0) {
    close(master_fd);
    setsid();
    ioctl(slave_fd, TIOCSCTTY, NULL);
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    dup2(slave_fd, STDERR_FILENO);
    if (slave_fd > STDERR_FILENO) {
      close(slave_fd);
    }
    char *args[] = {NULL};
    char *userShell = getenv("SHELL");
    if (strlen(userShell) == 0) {
      userShell = "/bin/sh";
    }
    execvp(userShell, args);
    perror("execvp failed");
    exit(EXIT_FAILURE);
  } else {
    close(slave_fd);
    while (true) {
      ssize_t n = read(master_fd, buffer, sizeof(buffer) - 1);
      if (n < 0) {
        shouldExit = 1;
        return 0;
      }
      pthread_mutex_lock(&mutex);
      tmt_write(vt, buffer, n);
      pthread_mutex_unlock(&mutex);
    }
  }
}
