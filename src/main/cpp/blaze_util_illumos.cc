// Copyright 2014 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Copied from the Linux version since Illumos also supports /proc.
//
//
// Following methods implemented specifically for Illumos:
// - GetSelfPath
// - GetStartTime (internal)
//   - WriteSystemSpecificProcessIdentifier
//   - VerifyServerProcess
// - GetProcessCWD

#include <errno.h>  // errno, ENAMETOOLONG
#include <fcntl.h>  // open
#include <limits.h>
#include <pwd.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // strerror
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <procfs.h>  // psinfo_t
#include <time.h>

#include "src/main/cpp/blaze_util.h"
#include "src/main/cpp/blaze_util_platform.h"
#include "src/main/cpp/util/errors.h"
#include "src/main/cpp/util/exit_code.h"
#include "src/main/cpp/util/file.h"
#include "src/main/cpp/util/logging.h"
#include "src/main/cpp/util/path.h"
#include "src/main/cpp/util/port.h"
#include "src/main/cpp/util/strings.h"

namespace blaze {

using blaze_util::GetLastErrorString;
using std::string;
using std::vector;

// ${XDG_CACHE_HOME}/bazel, a.k.a. ~/.cache/bazel by default (which is the
// fallback when XDG_CACHE_HOME is not set)
string GetOutputRoot() {
  string xdg_cache_home = GetPathEnv("XDG_CACHE_HOME");
  if (xdg_cache_home.empty()) {
    string home = GetHomeDir(); // via $HOME env variable
    if (home.empty()) {
      // Fall back to home dir from password database
      char buf[2048];
      struct passwd pwbuf;
      struct passwd *pw = nullptr;
      int uid = getuid();
      int r = getpwuid_r(uid, &pwbuf, buf, 2048, &pw);
      if (r == 0 && pw != nullptr) {
        home = pw->pw_dir;
      } else {
        return "/tmp";
      }
    }
    xdg_cache_home = blaze_util::JoinPath(home, ".cache");
  }

  return blaze_util::JoinPath(xdg_cache_home, "bazel");
}

void WarnFilesystemType(const blaze_util::Path &output_base) {
  // TODO: Implement on Illumos
}

string GetSelfPath(const char* argv0) {
  // illumos exposes the running executable as a symlink at "/proc/self/path/a.out".
  // This is always an absolute path, unlike getexecname which may return
  // a relative path.
  char buffer[PATH_MAX] = {};
  ssize_t bytes = readlink("/proc/self/path/a.out", buffer, sizeof(buffer));
  if (bytes == sizeof(buffer)) {
    // symlink contents truncated
    bytes = -1;
    errno = ENAMETOOLONG;
  }
  if (bytes == -1) {
    BAZEL_DIE(blaze_exit_code::INTERNAL_ERROR)
        << "error reading /proc/self/path/a.out: " << GetLastErrorString();
  }
  buffer[bytes] = '\0';  // readlink does not NUL-terminate
  return string(buffer);
}

uint64_t GetMillisecondsMonotonic() {
  struct timespec ts = {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
    BAZEL_DIE(blaze_exit_code::INTERNAL_ERROR)
        << "error calling clock_gettime: " << GetLastErrorString();
  }
  return ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000LL);
}

void SetScheduling(bool batch_cpu_scheduling, int io_nice_level) {
  // TODO: Implement setting CPU and IO scheduling hints.
}

std::unique_ptr<blaze_util::Path> GetProcessCWD(int pid) {
  char cwd[PATH_MAX] = {};
  string proc = "/proc/" + blaze_util::ToString(pid) + "/path/cwd";
  ssize_t len = readlink(proc.c_str(), cwd, sizeof(cwd) - 1);
  if (len <= 0) {
    // The process may have died, or /proc may be restricted. The caller
    // treats nullptr as "assume everything is alright", so we must NOT abort
    // here.
    return nullptr;
  }
  cwd[len] = '\0';
  return std::unique_ptr<blaze_util::Path>(new blaze_util::Path(string(cwd)));
}

bool IsSharedLibrary(const string &filename) {
  return blaze_util::ends_with(filename, ".so");
}

string GetSystemJavabase() {
  // if JAVA_HOME is defined, then use it as default.
  string javahome = GetPathEnv("JAVA_HOME");
  if (!javahome.empty()) {
    string javac = blaze_util::JoinPath(javahome, "bin/javac");
    if (access(javac.c_str(), X_OK) == 0) {
      return javahome;
    }
    BAZEL_LOG(WARNING)
        << "Ignoring JAVA_HOME, because there is no 'bin/javac'.";
  }

  // which javac
  string javac_dir = Which("javac");
  if (javac_dir.empty()) {
    return "";
  }

  // Resolve all symlinks.
  char resolved_path[PATH_MAX];
  if (realpath(javac_dir.c_str(), resolved_path) == nullptr) {
    return "";
  }
  javac_dir = resolved_path;

  // dirname dirname
  return blaze_util::Dirname(blaze_util::Dirname(javac_dir));
}

// Called from a signal handler!
static bool GetStartTime(const string& pid, string* start_time) {
  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "/proc/%s/psinfo", pid.c_str());

  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    return false;
  }

  psinfo_t info;
  ssize_t n = read(fd, &info, sizeof(info));
  close(fd);

  if (n != static_cast<ssize_t>(sizeof(info))) {
    return false;
  }

  // Process start time (wall clock). Combined with the PID this should be
  // unique. Nanoseconds are included for finer granularity against PID reuse.
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%ld.%09ld",
           static_cast<long>(info.pr_start.tv_sec),
           static_cast<long>(info.pr_start.tv_nsec));
  *start_time = string(buffer);
  return true;
}

int ConfigureDaemonProcess(posix_spawnattr_t* attrp,
                           const StartupOptions &options) {
  // No interesting platform-specific details to configure on this platform.
  return 0;
}

void WriteSystemSpecificProcessIdentifier(const blaze_util::Path& server_dir,
                                          pid_t server_pid) {
  string pid_string = blaze_util::ToString(server_pid);

  string start_time;
  if (!GetStartTime(pid_string, &start_time)) {
    BAZEL_DIE(blaze_exit_code::LOCAL_ENVIRONMENTAL_ERROR)
        << "Cannot get start time of process " << pid_string << ": "
        << GetLastErrorString();
  }

  blaze_util::Path start_time_file = server_dir.GetRelative("server.starttime");
  if (!blaze_util::WriteFile(start_time, start_time_file)) {
    BAZEL_DIE(blaze_exit_code::LOCAL_ENVIRONMENTAL_ERROR)
        << "Cannot write start time in server dir "
        << server_dir.AsPrintablePath() << ": " << GetLastErrorString();
  }
}

// On Linux we use a combination of PID and start time to identify the server
// process. That is supposed to be unique unless one can start more processes
// than there are PIDs available within a single jiffy.
bool VerifyServerProcess(int pid, const blaze_util::Path &output_base) {
  string start_time;
  if (!GetStartTime(blaze_util::ToString(pid), &start_time)) {
    // Cannot read PID file from /proc . Process died meantime, all is good. No
    // stale server is present.
    return false;
  }

  string recorded_start_time;
  bool file_present = blaze_util::ReadFile(
      output_base.GetRelative("server/server.starttime"), &recorded_start_time);

  // If start time file got deleted, but PID file didn't, assume that this is an
  // old Blaze process that doesn't know how to write start time files yet.
  return !file_present || recorded_start_time == start_time;
}

void ExcludePathFromBackup(const blaze_util::Path &path) {
  // Not supported.
}

int32_t GetExplicitSystemLimit(const int resource) {
  return -1;
}

}  // namespace blaze
