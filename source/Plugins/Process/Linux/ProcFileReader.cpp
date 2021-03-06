//===-- ProcFileReader.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/Linux/ProcFileReader.h"

// C Headers
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>

// C++ Headers
#include <fstream>

// LLDB Headers
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/Error.h"

using namespace lldb_private;
using namespace lldb_private::process_linux;

lldb::DataBufferSP ProcFileReader::ReadIntoDataBuffer(lldb::pid_t pid,
                                                      const char *name) {
  int fd;
  char path[PATH_MAX];

  // Make sure we've got a nil terminated buffer for all the folks calling
  // GetBytes() directly off our returned DataBufferSP if we hit an error.
  lldb::DataBufferSP buf_sp(new DataBufferHeap(1, 0));

  // Ideally, we would simply create a FileSpec and call ReadFileContents.
  // However, files in procfs have zero size (since they are, in general,
  // dynamically generated by the kernel) which is incompatible with the
  // current ReadFileContents implementation. Therefore we simply stream the
  // data into a DataBuffer ourselves.
  if (snprintf(path, PATH_MAX, "/proc/%" PRIu64 "/%s", pid, name) > 0) {
    if ((fd = open(path, O_RDONLY, 0)) >= 0) {
      size_t bytes_read = 0;
      std::unique_ptr<DataBufferHeap> buf_ap(new DataBufferHeap(1024, 0));

      for (;;) {
        size_t avail = buf_ap->GetByteSize() - bytes_read;
        ssize_t status = read(fd, buf_ap->GetBytes() + bytes_read, avail);

        if (status < 0)
          break;

        if (status == 0) {
          buf_ap->SetByteSize(bytes_read);
          buf_sp.reset(buf_ap.release());
          break;
        }

        bytes_read += status;

        if (avail - status == 0)
          buf_ap->SetByteSize(2 * buf_ap->GetByteSize());
      }

      close(fd);
    }
  }

  return buf_sp;
}

Error ProcFileReader::ProcessLineByLine(
    lldb::pid_t pid, const char *name,
    std::function<bool(const std::string &line)> line_parser) {
  Error error;

  // Try to open the /proc/{pid}/maps entry.
  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "/proc/%" PRIu64 "/%s", pid, name);
  filename[sizeof(filename) - 1] = '\0';

  std::ifstream proc_file(filename);
  if (proc_file.fail()) {
    error.SetErrorStringWithFormat("failed to open file '%s'", filename);
    return error;
  }

  // Read the file line by line, processing until either end of file or when the
  // line_parser returns false.
  std::string line;
  bool should_continue = true;

  while (should_continue && std::getline(proc_file, line)) {
    // Pass the line over to the line_parser for processing.  If the line_parser
    // returns false, we
    // stop processing.
    should_continue = line_parser(line);
  }

  return error;
}
