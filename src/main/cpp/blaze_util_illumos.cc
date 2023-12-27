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
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // strerror
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <procfs.h> // psinfo
#include <time.h>
#include <libproc.h> // proc_arg_psinfo

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

string GetOutputRoot() {
  string base;
  string home = GetHomeDir();
  if (!home.empty()) {
    base = home;
  } else {
    char buf[2048];
    struct passwd pwbuf;
    struct passwd *pw = NULL;
    int uid = getuid();
    int r = getpwuid_r(uid, &pwbuf, buf, 2048, &pw);
    if (r != -1 && pw != NULL) {
      base = pw->pw_dir;
    }
  }

  if (!base.empty()) {
    return blaze_util::JoinPath(base, ".cache/bazel");
  }

  return "/tmp";
}

void WarnFilesystemType(const blaze_util::Path &output_base) {
  // TODO: Implement on Illumos
}

string GetSelfPath(const char* argv0) {
  return string(getexecname());
}

uint64_t GetMillisecondsMonotonic() {
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000LL);
}

uint64_t GetMillisecondsSinceProcessStart() {
  struct timespec ts = {};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return ts.tv_sec * 1000LL + (ts.tv_nsec / 1000000LL);
}

void SetScheduling(bool batch_cpu_scheduling, int io_nice_level) {
  // TODO: Implement setting CPU and IO scheduling hints.
}

std::unique_ptr<blaze_util::Path> GetProcessCWD(int pid) {
  // In Illumos and Solaris using readlink on '/proc/PID/cwd' in procfs (like is done in the Linux port) will result
  // in a null string. This implementation inspired on how 'pwdx.c' (in base Illumos) does it.

  char cwd[PATH_MAX], proc[128];
  psinfo_t psinfo;
  int err_code, ret_readlink;

  if (proc_arg_psinfo(std::to_string(pid).c_str(), PR_ARG_PIDS, &psinfo, &err_code) == -1) {
    BAZEL_DIE(blaze_exit_code::INTERNAL_ERROR)
      << "Unable to retrieve 'psinfo_t' for PID: " << pid;
  }

  (void) snprintf(proc, sizeof (proc), "/proc/%d/path/cwd", (int)psinfo.pr_pid);

  if ((ret_readlink = readlink(proc, cwd, sizeof (cwd) - 1)) <= 0) {
    BAZEL_DIE(blaze_exit_code::INTERNAL_ERROR)
      << "Cannot resolve current work dir for PID: " << pid;
  }
  cwd[ret_readlink] = '\0';

  BAZEL_LOG(INFO) << "PID: " << pid << " (" << string(cwd) << ").";

  return std::unique_ptr<blaze_util::Path>(new blaze_util::Path(string(cwd)));
}

bool IsSharedLibrary(const string &filename) {
  return blaze_util::ends_with(filename, ".so");
}

string GetSystemJavabase() {
  // if JAVA_HOME is defined, then use it as default.
  string javahome = GetEnv("JAVA_HOME");
  if (!javahome.empty()) {
    return javahome;
  }

  // which javac
  string javac_dir = Which("javac");
  if (javac_dir.empty()) {
    return "";
  }

  // Resolve all symlinks.
  char resolved_path[PATH_MAX];
  if (realpath(javac_dir.c_str(), resolved_path) == NULL) {
    return "";
  }
  javac_dir = resolved_path;

  // dirname dirname
  return blaze_util::Dirname(blaze_util::Dirname(javac_dir));
}

// Called from a signal handler!
static bool GetStartTime(const string& pid, string* start_time) {
  // TODO: Threw this together somewhat quick. Needs to be checked
  // for correctness.
  char filename [PATH_MAX];
  FILE *f;
  bool time_set = false;
  psinfo_t info;

  snprintf(filename, sizeof(filename), "/proc/%s/psinfo", pid.c_str());

  f = fopen(filename, "r");
  if (f) {
    if (fread(&info, sizeof(info), 1, f) > 0) {
      char buffer [128];
      sprintf (buffer, "%lu", info.pr_start.tv_sec);
      *start_time = string(buffer);
      time_set = true;
    }
    
    fclose(f);
  }

  return time_set;
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
        << "Cannot get start time of process " << pid_string.c_str();
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

  return recorded_start_time == start_time;
}

void ExcludePathFromBackup(const blaze_util::Path &path) {
  // Not supported.
}

int32_t GetExplicitSystemLimit(const int resource) {
  return -1;
}

}  // namespace blaze
