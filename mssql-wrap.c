#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/reg.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <linux/sched.h>
#include <linux/ptrace.h>

static const char* prefix = "/opt/microsoft/sqlncli/11.0.1790.0/en_US/";

const char* read_string(pid_t pid, long addr)
{
  static char buf[1024];

  for(int i = 0; /**/; i += sizeof(long))
  {
    long data = ptrace(PTRACE_PEEKTEXT, pid, (void*)(addr + i), NULL);

    for(int j = 0; j < sizeof(long); j++)
    {
      char c = data & UCHAR_MAX;
      data >>= sizeof(char)*CHAR_BIT;

      buf[i + j] = c;

      if(!c)
        return buf;
      if(i == sizeof(buf) - 1)
        return NULL;
    }
  }

  return buf;
}

int main(int argc, char* argv[])
{
  if(argc < 2) {
    fprintf(stderr, "Usage: %s path [args...]\n", argv[0]);
    return EXIT_FAILURE;
  }

  pid_t child = fork();
  if(child < 0) {
    perror("fork");
    return EXIT_FAILURE;
  }

  if(child == 0) {
    // child
    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    execv(argv[1], argv + 1);
    perror("execv");
    return EXIT_FAILURE;
  }

  // parent

  // wait for first SIGSTOP
  wait(NULL);
  ptrace(PTRACE_SETOPTIONS, child, NULL, (void*)PTRACE_O_TRACECLONE);
  ptrace(PTRACE_SYSCALL, child, NULL, NULL);

  // main loop!
  int entering_syscall = 1;
  int remap_count = 0;
  for(;;) {
    int status;
    pid_t pid = waitpid(-1, &status, __WALL);

    if(WIFEXITED(status)) {
      if(pid == child)
        return WEXITSTATUS(status);
    }
    else if(WIFSIGNALED(status)) {
      if(pid == child) {
        fprintf(stderr, "Child terminated by signal %d\n", WTERMSIG(status));
        return EXIT_FAILURE;
      }
      long sig = WTERMSIG(status);
      ptrace(PTRACE_CONT, pid, NULL, (void*)sig);
    }
    else if(WIFSTOPPED(status))
    {
      if(WSTOPSIG(status) == SIGTRAP)
      {
        int detach = 0;

        if(entering_syscall)
        {
          long orig_rax = ptrace(PTRACE_PEEKUSER, pid, 8 * ORIG_RAX, NULL);
          if(orig_rax == SYS_open)
          {
            long rdi = ptrace(PTRACE_PEEKUSER, pid, 8 * RDI, NULL);
            const char* path = read_string(pid, rdi);
            if(path)
            {
              if(strstr(path, prefix) == path)
              {
                fprintf(stderr, "open(%s)\n", path);
                rdi += strlen(prefix);
                ptrace(PTRACE_POKEUSER, pid, 8 * RDI, (void*)rdi);

                remap_count++;
                if(strstr(argv[1], "/sqlcmd")) {
                  if(remap_count == 4)
                    detach = 1; // remap 4 files before detaching
                }
                else if(strstr(argv[1], "/bcp")) {
                  if(remap_count == 2)
                    detach = 1; // remap 2 files before detaching
                }
              }
            }
          }
        }
        entering_syscall = !entering_syscall;

        if(detach) {
          fprintf(stderr, "detaching from process\n");
          ptrace(PTRACE_DETACH, pid, NULL, NULL);
        }
        else {
          ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
        }
      }
      else if(WSTOPSIG(status) == SIGSTOP)
      {
        ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
      }
      else
      {
        fprintf(stderr, "Some other signal delivered to process: %d\n", WSTOPSIG(status));
        ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
      }
    }
    else
    {
      fprintf(stderr, "Something else happened?\n");
    }
  } // end loop
} // end main

