#pragma once

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_RDWR   0x2
#define O_CREAT  0x4

#define WNOHANG 0x1


int open(const char *path, int flags, ...);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int close(int fd);
int unlink(const char *path);
void wait(uint32_t ms);
pid_t spawn(const char *path, const char *argv[]);
char *find_program(const char *name, char *resolved);
pid_t spawnp(const char *file, const char *argv[]);
int kill(pid_t pid);
pid_t getpid(void);
int waitpid(pid_t pid, uint32_t flags);

#define TASK_RUNNING  0
#define TASK_EXITED   1
#define TASK_DETACHED 2
int task_status(pid_t pid, int *code);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
char *realpath(const char *path, char *resolved);
int dup2(int fd, int target);
int execvp(const char *file, char *const argv[]);

extern char **environ;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int64_t lseek(int fd, int64_t offset, int whence);

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int     access(const char *path, int mode);
int     rmdir(const char *path);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int     isatty(int fd);
unsigned int sleep(unsigned int seconds);
int     usleep(useconds_t usec);
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
int     gethostname(char *name, size_t len);
