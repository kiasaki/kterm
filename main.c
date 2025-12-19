#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <errno.h>
#include <pthread.h>
#include "lib/raylib.h"
#include "lib/go.h"
#include "lib/tmt.h"
#include "lib/tmt.c"

const int fontSize = 18;
const int spacing = 0;
int shouldExit = 0;
TMT *vt;
int master_fd, slave_fd;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void die(char*);
int max(int a, int b);
void callback(tmt_msg_t m, TMT *vt, const void *a, void *p);
void* start_shell(void* param);

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(1024, 768, "term");
    SetWindowMinSize(400, 400);
    Font font = LoadFont_Go();
    Vector2 charSize = MeasureTextEx(font, " ", fontSize, spacing);
    SetTargetFPS(60);

    Color colorFg = GetColor(0x373B41FF);
    Color colorBg = GetColor(0xFFFFFFFF);
    Color colors[9] = {
      GetColor(0x373B41FF),
      GetColor(0x1D1F21FF),
      GetColor(0xCC6666FF),
      GetColor(0xB5BD68FF),
      GetColor(0xF0C674FF),
      GetColor(0x81A2BEFF),
      GetColor(0xB294BBFF),
      GetColor(0x8ABEB7FF),
      GetColor(0xC5C8C6FF),
    };

    setenv("TERM", "linux", 1);
    vt = tmt_open((GetScreenHeight()-8)/fontSize, (GetScreenWidth()-8)/charSize.x, callback, NULL, NULL);
    if (!vt) die("can not open virtual terminal");

    pthread_t thread;
    pthread_create(&thread, NULL, start_shell, NULL);

    while (!WindowShouldClose() && !shouldExit) {
      if (IsWindowResized()) {
        Vector2 dpi = GetWindowScaleDPI();
        tmt_resize(vt, (GetRenderHeight()/dpi.y-8)/fontSize, (GetRenderWidth()/dpi.x-8)/charSize.x);
    printf("h %d w %d f %d c %d row %d col %d\n", GetScreenHeight(), GetScreenWidth(), GetRenderHeight(), GetRenderWidth(), vt->screen.nline, vt->screen.ncol);
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
      if (IsKeyPressed(KEY_TAB)) write(master_fd, "\t", 1); 
      if (IsKeyPressed(KEY_ENTER)||IsKeyPressedRepeat(KEY_ENTER)) write(master_fd, "\r", 1); 
      if (IsKeyPressed(KEY_ESCAPE)) write(master_fd, TMT_KEY_ESCAPE, 1); 
      if (IsKeyPressed(KEY_BACKSPACE)) write(master_fd, TMT_KEY_BACKSPACE, 1); 
      if (IsKeyPressed(KEY_LEFT)) write(master_fd, TMT_KEY_LEFT, 3); 
      if (IsKeyPressed(KEY_DOWN)) write(master_fd, TMT_KEY_DOWN, 3); 
      if (IsKeyPressed(KEY_UP)) write(master_fd, TMT_KEY_UP, 3); 
      if (IsKeyPressed(KEY_RIGHT)) write(master_fd, TMT_KEY_RIGHT, 3); 
      pthread_mutex_unlock(&mutex);

      BeginDrawing();
      ClearBackground(colorBg);
      const TMTSCREEN *s = tmt_screen(vt);
      const TMTPOINT *cu = tmt_cursor(vt);
      char ch[2]; ch[1] = '\0';
      DrawRectangle(4+cu->c*charSize.x, 4+cu->r*fontSize, charSize.x, fontSize, colorFg);
      for (size_t r = 0; r < s->nline; r++) {
        for (size_t c = 0; c < s->ncol; c++) {
          TMTCHAR tmc = s->lines[r]->chars[c];
          ch[0] = tmc.c;
          Vector2 p = {4+c*charSize.x, 4+r*fontSize};
          Color bg = r==cu->r && c==cu->c ? colorFg : colors[max(0, tmc.a.bg)];
          Color fg = r==cu->r && c==cu->c ? colorBg : colors[max(0, tmc.a.fg)];
          if (tmc.a.bg > 0) DrawRectangle(p.x, p.y, charSize.x, fontSize, bg);
          DrawTextEx(font, &ch[0], p, fontSize, spacing, fg);
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
  if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) < 0) {
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
