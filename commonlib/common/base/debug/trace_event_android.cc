// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/trace_event_impl.h"

#include <fcntl.h>

#include "base/debug/trace_event.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/stringprintf.h"

namespace {

int g_atrace_fd = -1;
const char* kATraceMarkerFile = "/sys/kernel/debug/tracing/trace_marker";

void WriteEvent(char phase,
                const char* category,
                const char* name,
                unsigned long long id,
                int num_args,
                const char** arg_names,
                const unsigned char* arg_types,
                const unsigned long long* arg_values,
                unsigned char flags) {
  std::string out = base::StringPrintf("%c|%d|%s", phase, getpid(), name);
  if (flags & TRACE_EVENT_FLAG_HAS_ID)
    base::StringAppendF(&out, "-%" PRIx64, static_cast<uint64>(id));
  out += '|';

  for (int i = 0; i < num_args; ++i) {
    if (i)
      out += ';';
    out += arg_names[i];
    out += '=';
    base::debug::TraceEvent::TraceValue value;
    value.as_uint = arg_values[i];
    std::string::size_type value_start = out.length();
    base::debug::TraceEvent::AppendValueAsJSON(arg_types[i], value, &out);
    // Remove the quotes which may confuse the atrace script.
    ReplaceSubstringsAfterOffset(&out, value_start, "\\\"", "'");
    ReplaceSubstringsAfterOffset(&out, value_start, "\"", "");
    // Replace chars used for separators with similar chars in the value.
    std::replace(out.begin() + value_start, out.end(), ';', ',');
    std::replace(out.begin() + value_start, out.end(), '|', '!');
  }

  out += '|';
  out += category;
  write(g_atrace_fd, out.c_str(), out.size());
}

}  // namespace

namespace base {
namespace debug {

void TraceLog::StartATrace() {
  AutoLock lock(lock_);
  if (g_atrace_fd == -1) {
    g_atrace_fd = open(kATraceMarkerFile, O_WRONLY);
    if (g_atrace_fd == -1)
      LOG(WARNING) << "Couldn't open " << kATraceMarkerFile;
  }
}

void TraceLog::StopATrace() {
  AutoLock lock(lock_);
  if (g_atrace_fd != -1) {
    close(g_atrace_fd);
    g_atrace_fd = -1;
  }
}

void TraceLog::SendToATrace(char phase,
                            const char* category,
                            const char* name,
                            unsigned long long id,
                            int num_args,
                            const char** arg_names,
                            const unsigned char* arg_types,
                            const unsigned long long* arg_values,
                            unsigned char flags) {
  if (g_atrace_fd == -1)
    return;

  switch (phase) {
    case TRACE_EVENT_PHASE_BEGIN:
      WriteEvent('B', category, name, id,
                 num_args, arg_names, arg_types, arg_values, flags);
      break;

    case TRACE_EVENT_PHASE_END:
      // Though a single 'E' is enough, here append pid, name and category etc.
      // so that unpaired events can be found easily.
      WriteEvent('E', category, name, id,
                 num_args, arg_names, arg_types, arg_values, flags);
      break;

    case TRACE_EVENT_PHASE_INSTANT:
      // Simulate an instance event with a pair of begin/end events.
      WriteEvent('B', category, name, id,
                 num_args, arg_names, arg_types, arg_values, flags);
      write(g_atrace_fd, "E", 1);
      break;

    case TRACE_EVENT_PHASE_COUNTER:
      for (int i = 0; i < num_args; ++i) {
        DCHECK(arg_types[i] == TRACE_VALUE_TYPE_INT);
        std::string out = base::StringPrintf("C|%d|%s-%s",
                                       getpid(), name, arg_names[i]);
        if (flags & TRACE_EVENT_FLAG_HAS_ID)
          StringAppendF(&out, "-%" PRIx64, static_cast<uint64>(id));
        StringAppendF(&out, "|%d|%s",
                      static_cast<int>(arg_values[i]), category);
        write(g_atrace_fd, out.c_str(), out.size());
      }
      break;

    default:
      // Do nothing.
      break;
  }
}

// Must be called with lock_ locked.
void TraceLog::ApplyATraceEnabledFlag(unsigned char* category_enabled) {
  if (g_atrace_fd != -1)
    *category_enabled |= ATRACE_ENABLED;
  else
    *category_enabled &= ~ATRACE_ENABLED;
}

}  // namespace debug
}  // namespace base
