/* ClassiCube platform layer for StrawOS: files, time, sockets and process glue over the
   StrawOS libc. There are no threads (see config.h), so the threading API is stubbed. */
#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_THREADING
#define CC_NO_CRASHHANDLER

#include "Stream.h"
#include "ExtMath.h"
#include "SystemFonts.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "PackedCol.h"

#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ioctl.h>
#include <poll.h>
#include <socket.h>
#include <net.h>
#include <sys/stat.h>

/* where the game keeps options, maps, texture packs and screenshots */
#define GAME_DIR "/share/classicube"

/* kernel calls report failure without a reason; these stand in for one */
const cc_result ReturnCode_FileShareViolation = 1000000000; /* not used */
const cc_result ReturnCode_FileNotFound       = ENOENT;
const cc_result ReturnCode_PathNotFound       = 99999;
const cc_result ReturnCode_DirectoryExists    = EEXIST;

const cc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const cc_result ReturnCode_SocketWouldBlock = EAGAIN;
const cc_result ReturnCode_SocketDropped    = EPIPE;

const char* Platform_AppNameSuffix = "";
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS;
cc_bool  Platform_ReadonlyFilesystem;
#include "_PlatformBase.h"

/* StrawOS calls return a negative errno (or just -1) on failure */
static cc_result ResultOf(long rc, cc_result fallback) {
	if (rc >= 0) return 0;
	return rc == -1 ? fallback : (cc_result)-rc;
}


/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "main_impl.h"

int main(int argc, char** argv) {
	cc_result res;
	SetupProgram(argc, argv);

	/* the launcher and the game share the process (and the window): starting the game from
	   the launcher returns here with the game's arguments set */
	do {
		res = RunProgram(argc, argv);
	} while (Window_Main.Exists);

	Window_Free();
	Process_Exit(res);
	return res;
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	write(STDOUT_FILENO, msg, len);
	write(STDOUT_FILENO, "\n", 1);
}

TimeMS DateTime_CurrentUTC(void) {
	return (cc_uint64)time(NULL) + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	time_t now = time(NULL);
	struct tm loc;
	localtime_r(&now, &loc);

	t->year   = loc.tm_year + 1900;
	t->month  = loc.tm_mon  + 1;
	t->day    = loc.tm_mday;
	t->hour   = loc.tm_hour;
	t->minute = loc.tm_min;
	t->second = loc.tm_sec;
}

/* the kernel clock ticks in milliseconds */
cc_uint64 Stopwatch_Measure(void) {
	return uptime_ms() * 1000;
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	String_EncodeUtf8(dst->buffer, path);
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	const char* str = path->buffer;
	String_AppendUtf8(dst, str, String_Length(str));
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	struct stat sb;
	if (stat(path->buffer, &sb) == 0) return EEXIST;
	return ResultOf(mkdir(path->buffer, 0755), ReturnCode_PathNotFound);
}

int File_Exists(const cc_filepath* path) {
	struct stat sb;
	return stat(path->buffer, &sb) == 0 && sb.type == STAT_FILE;
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_string path; char pathBuffer[FILENAME_SIZE];
	cc_filepath str;
	struct dirent entry;
	char name[NAME_MAX + 1];
	int fd;

	Platform_EncodePath(&str, dirPath);
	fd = open(str.buffer, O_RDONLY);
	if (fd < 0) return ReturnCode_PathNotFound;
	String_InitArray(path, pathBuffer);

	while (readdir(fd, &entry) == 1) {
		Mem_Copy(name, entry.name, NAME_MAX);
		name[NAME_MAX] = '\0';
		if (name[0] == '.' && name[1] == '\0') continue;
		if (name[0] == '.' && name[1] == '.' && name[2] == '\0') continue;

		path.length = 0;
		String_Format1(&path, "%s/", dirPath);
		String_AppendUtf8(&path, name, String_Length(name));
		callback(&path, obj, entry.type == STAT_DIR);
	}

	close(fd);
	return 0;
}

static cc_result File_Do(cc_file* file, const char* path, int mode) {
	int fd = open(path, mode);
	*file  = fd;
	return ResultOf(fd, ReturnCode_FileNotFound);
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	if (!File_Exists(path)) { *file = -1; return ReturnCode_FileNotFound; }
	return File_Do(file, path->buffer, O_RDONLY);
}
cc_result File_Create(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}
cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	ssize_t n = read(file, data, count);
	*bytesRead = n < 0 ? 0 : (cc_uint32)n;
	return ResultOf(n, EIO);
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	ssize_t n = write(file, data, count);
	*bytesWrote = n < 0 ? 0 : (cc_uint32)n;
	return ResultOf(n, EIO);
}

cc_result File_Close(cc_file file) {
	return ResultOf(close(file), EBADF);
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static const int modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return ResultOf((long)lseek(file, offset, modes[seekType]), ESPIPE);
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	int64_t p = lseek(file, 0, SEEK_CUR);
	*pos = p < 0 ? 0 : (cc_uint32)p;
	return ResultOf((long)p, ESPIPE);
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	struct stat sb;
	if (fstat(file, &sb) != 0) { *len = 0; return EBADF; }
	*len = (cc_uint32)sb.size;
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
	if (milliseconds) wait(milliseconds);
	else yield();
}


/*########################################################################################################################*
*--------------------------------------------------------Fonts------------------------------------------------------------*
*#########################################################################################################################*/
static void FontDirCallback(const cc_string* path, void* obj, int isDirectory) {
	if (isDirectory) {
		Directory_Enum(path, NULL, FontDirCallback);
	} else {
		SysFonts_Register(path, NULL);
	}
}

void Platform_LoadSysFonts(void) {
	static const cc_string dir = String_FromConst("/share/fonts");
	Directory_Enum(&dir, NULL, FontDirCallback);
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
/* the kernel takes TCP peers as "a.b.c.d:port" strings */
struct StrawAddr { cc_uint8 ip[4]; cc_uint16 port; };

static void StrawAddr_Set(cc_sockaddr* dst, const cc_uint8* ip, int port) {
	struct StrawAddr a;
	Mem_Copy(a.ip, ip, 4);
	a.port = (cc_uint16)port;
	SocketAddr_Set(dst, &a, sizeof(a));
}

static cc_bool ParseIPv4(const cc_string* ip, int port, cc_sockaddr* dst) {
	cc_uint32 raw;
	if (!ParseIPv4Address(ip, &raw)) return false;

	StrawAddr_Set(dst, (const cc_uint8*)&raw, port);
	return true;
}

static cc_bool ParseIPv6(const char* ip, int port, cc_sockaddr* dst) {
	return false;
}

static cc_result ParseHost(const char* host, int port, cc_sockaddr* addrs, int* numValidAddrs) {
	cc_uint8 ip[4];
	int rc = net_resolve(host, ip);
	if (rc == -ENETDOWN) return ENETDOWN;
	if (rc != 0) return ERR_INVALID_ARGUMENT;

	StrawAddr_Set(&addrs[0], ip, port);
	*numValidAddrs = 1;
	return 0;
}

cc_bool SockAddr_ToString(const cc_sockaddr* addr, cc_string* dst) {
	const struct StrawAddr* a = (const struct StrawAddr*)addr->data;
	int port = a->port;

	String_Format4(dst, "%b.%b.%b.%b", &a->ip[0], &a->ip[1], &a->ip[2], &a->ip[3]);
	String_Format1(dst, ":%i", &port);
	return true;
}

cc_result Socket_Create(cc_socket* s, cc_sockaddr* addr) {
	int fd = socket(SOCK_TCP);
	*s = fd;
	return ResultOf(fd, EMFILE);
}

cc_result Socket_SetNonBlocking(cc_socket s, cc_bool nonblocking) {
	int on = nonblocking;
	return ResultOf(ioctl(s, SOCK_IOCTL_NONBLOCK, &on), EINVAL);
}

void Socket_Close(cc_socket s) {
	close(s);
}

cc_result Socket_Connect(cc_socket s, const void* addr, int addrSize) {
	const struct StrawAddr* a = (const struct StrawAddr*)addr;
	char name[32];

	snprintf(name, sizeof(name), "%u.%u.%u.%u:%u", a->ip[0], a->ip[1], a->ip[2], a->ip[3], a->port);
	return ResultOf(connect(s, name), ECONNREFUSED);
}

cc_result Socket_Read(cc_socket s, cc_uint8* data, cc_uint32 count, cc_uint32* modified) {
	ssize_t n = read(s, data, count);
	*modified = n < 0 ? 0 : (cc_uint32)n;
	return ResultOf(n, EIO);
}

cc_result Socket_Write(cc_socket s, const cc_uint8* data, cc_uint32 count, cc_uint32* modified) {
	ssize_t n = write(s, data, count);
	*modified = n < 0 ? 0 : (cc_uint32)n;
	return ResultOf(n, EIO);
}

cc_result Socket_Poll(cc_socket s, int timeoutMS, int mode, cc_bool* success) {
	struct pollfd pfd;
	pfd.fd      = s;
	pfd.events  = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	pfd.revents = 0;

	if (poll(&pfd, 1, timeoutMS) < 0) { *success = false; return EIO; }

	if (mode == SOCKET_POLL_READ) {
		/* a closed socket still counts as readable, as with select */
		*success = (pfd.revents & (POLLIN | POLLHUP)) != 0;
		return 0;
	}

	/* a refused or reset connect never becomes writable: report it instead of timing out */
	*success = (pfd.revents & POLLOUT) != 0;
	if (!*success && (pfd.revents & (POLLHUP | POLLERR))) return ECONNREFUSED;
	return 0;
}


/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

void Process_Exit(cc_result code) { exit(code); }

/* links (the launcher's "ClassiCube.net" buttons, chat URLs) open in NetSurf */
cc_bool Process_OpenSupported = true;

cc_result Process_StartOpen(const cc_string* args) {
	char url[NATIVE_STR_LEN];
	const char* argv[3];

	String_EncodeUtf8(url, args);
	argv[0] = "/bin/netsurf.elf";
	argv[1] = url;
	argv[2] = NULL;
	return spawn(argv[0], argv) < 0 ? ERR_NOT_SUPPORTED : 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	const char* msg;
	if (res >= 1000) return false;

	msg = strerror((int)res);
	if (!msg || !msg[0]) return false;
	String_AppendUtf8(dst, msg, String_Length(msg));
	return true;
}

void Platform_Init(void) { }

/* saved passwords would need a machine key to encrypt them with; ClassiCube simply skips
   remembering them when this fails */
cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

/* only seeds BearSSL's DRBG: mix the cycle counter (jittery under scheduling and I/O) with
   the clocks */
cc_result Platform_GetEntropy(void* data, int len) {
	cc_uint8* dst = (cc_uint8*)data;
	cc_uint64 x = ((cc_uint64)time(NULL) << 32) ^ uptime_ms() ^ (cc_uint64)(cc_uintptr)data;
	int i;

	for (i = 0; i < len; i++) {
		cc_uint32 lo, hi;
		__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
		x ^= ((cc_uint64)hi << 32) | lo;
		/* splitmix64 */
		x += 0x9E3779B97F4A7C15ULL;
		cc_uint64 z = x;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		dst[i] = (cc_uint8)(z ^ (z >> 31));
	}
	return 0;
}


/*########################################################################################################################*
*-----------------------------------------------------Configuration-------------------------------------------------------*
*#########################################################################################################################*/
int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
	int i, count;
	argc--; argv++; /* skip executable path argument */
	if (gameHasArgs) return GetGameArgs(args);

	count = min(argc, GAME_MAX_CMDARGS);
	for (i = 0; i < count; i++)
	{
		args[i] = String_FromReadonly(argv[i]);
	}
	return count;
}

cc_result Platform_SetDefaultCurrentDirectory(void) {
	struct stat sb;
	if (stat(GAME_DIR, &sb) != 0) mkdir(GAME_DIR, 0755);
	return ResultOf(chdir(GAME_DIR), ReturnCode_PathNotFound);
}
