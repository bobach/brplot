#include "src/br_plotter.h"

#include "dlfcn.h"
#include "errno.h"
#include "poll.h"
#include "pthread.h"
#include "signal.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/inotify.h"
#include "sys/select.h"
#include "sys/syscall.h"
#include "unistd.h"
#include <bits/types/siginfo_t.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#define GCC "/bin/g++"

#ifndef IMGUI
#error "IMGUI must be defined"
#endif
#ifdef RELEASE
#error "RELEASE must not be defined"
#endif

static bool br_hotreload_compile(void) {
  pid_t a = fork();
  if (a == -1) {
    perror("Fork failed");
    return false;
  }
  if (a == 0) {
    static char* newargv[] = { GCC, "-Iexternal/raylib-5.0/src", "-Iexternal/imgui-docking", "-I.", "-DLINUX", "-DPLATFORM_DESKTOP", "-fpic", "--shared", "-g", "-o", "build/hot.o", "src/imgui/hot.cpp", NULL };
    execvp(GCC, newargv);
  } else {
    int wstat;
    waitpid(a, &wstat, WUNTRACED | WCONTINUED);
  }
  return true;
}
//TODO: Check This shit up
//#define RTLD_DEEPBIND	0x00008	/* Use deep binding.  */
//
///* If the following bit is set in the MODE argument to `dlopen',
//   the symbols of the loaded object and its dependencies are made
//   visible as if the object were linked directly into the program.  */
//#define RTLD_GLOBAL	0x00100

void br_hotreload_link(br_hotreload_state_t* s) {
  s->handl = dlopen("build/hot.o", RTLD_GLOBAL |	RTLD_LAZY);
  if (s->handl == NULL) {
    const char* err = dlerror();
    fprintf(stderr, "dlopen failed: `%s`\n", err ? err : "NULL");
    return;
  }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
  s->func_loop = (void (*)(br_plotter_t*))dlsym(s->handl, "br_hot_loop");
  s->func_init = (void (*)(br_plotter_t*))dlsym(s->handl, "br_hot_init");
  printf("hot opened init: %p, loop: %p\n", s->func_loop, s->func_init);
#pragma GCC diagnostic pop
  char* error = dlerror();
  if (error != NULL) {
    fprintf(stderr, "%s\n", error);
    dlclose(s->handl);
    s->handl = NULL;
    s->func_loop = NULL;
    s->func_init = NULL;
  }
  s->is_init_called = false;
}

static void hot_reload_all(br_hotreload_state_t* state) {
  if (br_hotreload_compile()) {
    if (state->func_loop != NULL) {
      pthread_mutex_lock(&state->lock);
      dlclose(state->handl);
      printf("dlclosed\n");
      state->handl = NULL;
      state->func_loop = NULL;
      state->func_init = NULL;
      state->is_init_called = false;
      pthread_mutex_unlock(&state->lock);
    }
    br_hotreload_link(state);
  }
}

static void* hot_reload_loop(void* s) {
  br_hotreload_state_t* state = (br_hotreload_state_t*)s;
  int fd = inotify_init();
  static uint32_t buff[512];
  int wd = inotify_add_watch(fd, "src/imgui", IN_MODIFY);
  if (wd < 0) {
    perror("INIT NOTIFY ERROR");
  }
  while(true) {
    ssize_t len = read(fd, buff, sizeof(buff));
    fprintf(stderr, "read %ld bytes\n", len);
    if (len == -1) {
      perror("NOTIFY ERROR");
    }
    if (len <= 0)
      continue;
    hot_reload_all(state);
  }
  return NULL;
}

void br_hotreload_start(br_hotreload_state_t* state) {
  hot_reload_all(state);
  pthread_t thread2;
  pthread_attr_t attrs2;
  pthread_attr_init(&attrs2);
  if (pthread_create(&thread2, &attrs2, hot_reload_loop, state)) {
    fprintf(stderr, "ERROR while creating thread %d:`%s`\n", errno, strerror(errno));
  }
}
