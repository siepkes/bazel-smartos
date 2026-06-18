// Copyright 2015 The Bazel Authors. All rights reserved.
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

#include "src/main/native/unix_jni.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

namespace blaze_jni {

using std::string;

// See unix_jni.h.
string ErrorMessage(int error_number) {
  char buf[1024] = "";
  if (strerror_r(error_number, buf, sizeof buf) != 0) {
    snprintf(buf, sizeof buf, "strerror_r(%d): errno %d", error_number, errno);
  }

  return string(buf);
}

int portable_fstatat(int dirfd, char *name, portable_stat_struct *statbuf,
                     int flags) {
  return fstatat(dirfd, name, statbuf, flags);
}

int StatSeconds(const portable_stat_struct &statbuf, StatTimes t) {
  switch (t) {
    case STAT_ATIME:
      return statbuf.st_atim.tv_sec;
    case STAT_CTIME:
      return statbuf.st_ctim.tv_sec;
    case STAT_MTIME:
      return statbuf.st_mtim.tv_sec;
  }
  return 0;
}

int StatNanoSeconds(const portable_stat_struct &statbuf, StatTimes t) {
  switch (t) {
    case STAT_ATIME:
      return statbuf.st_atim.tv_nsec;
    case STAT_CTIME:
      return statbuf.st_ctim.tv_nsec;
    case STAT_MTIME:
      return statbuf.st_mtim.tv_nsec;
  }
  return 0;
}

ssize_t portable_getxattr(const char *path, const char *name, void *value,
                          size_t size, bool *attr_not_found) {
  *attr_not_found = true;
  return -1;
}

ssize_t portable_lgetxattr(const char *path, const char *name, void *value,
                           size_t size, bool *attr_not_found) {
  *attr_not_found = true;
  return -1;
}

int portable_push_disable_sleep() {
  // Currently not implemented.
  return -1;
}

int portable_pop_disable_sleep() {
  // Currently not implemented.
  return -1;
}

void portable_start_suspend_monitoring() {
  // Currently not implemented.
}

void portable_start_thermal_monitoring() {
  // Currently not implemented.
}

int portable_thermal_load() {
  // Currently not implemented.
  return 0;
}

void portable_start_system_load_advisory_monitoring() {
  // Currently not implemented.
}

int portable_system_load_advisory() {
  // Currently not implemented.
  return 0;
}

void portable_start_memory_pressure_monitoring() {
  // Currently not implemented.
}

MemoryPressureLevel portable_memory_pressure() {
  // Currently not implemented.
  return MemoryPressureLevelNormal;
}

void portable_start_disk_space_monitoring() {
  // Currently not implemented.
}

void portable_start_cpu_speed_monitoring() {
  // Currently not implemented.
}

int portable_cpu_speed() {
  // Currently not implemented.
  return -1;
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_devtools_build_lib_profiler_SystemNetworkStats_getNetIoCountersNative(
    JNIEnv *env, jclass clazz, jobject counters_list) {
  // Currently not implemented.
}

}  // namespace blaze_jni
