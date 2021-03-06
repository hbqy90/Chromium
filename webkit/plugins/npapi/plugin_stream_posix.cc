// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/npapi/plugin_stream.h"

#include <string.h>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "webkit/plugins/npapi/plugin_instance.h"

namespace webkit {
namespace npapi {

void PluginStream::ResetTempFileHandle() {
  temp_file_ = NULL;
}

void PluginStream::ResetTempFileName() {
  temp_file_path_ = FilePath();
}

void PluginStream::WriteAsFile() {
  if (requested_plugin_mode_ == NP_ASFILE ||
      requested_plugin_mode_ == NP_ASFILEONLY) {
    instance_->NPP_StreamAsFile(&stream_, temp_file_path_.value().c_str());
  }
}

size_t PluginStream::WriteBytes(const char* buf, size_t length) {
  return fwrite(buf, sizeof(char), length, temp_file_);
}

bool PluginStream::OpenTempFile() {
  DCHECK_EQ(static_cast<FILE*>(NULL), temp_file_);

  if (file_util::CreateTemporaryFile(&temp_file_path_))
    temp_file_ = file_util::OpenFile(temp_file_path_, "a");

  if (!temp_file_) {
    file_util::Delete(temp_file_path_, false);
    ResetTempFileName();
    return false;
  }
  return true;
}

void PluginStream::CloseTempFile() {
  if (!TempFileIsValid())
    return;

  file_util::CloseFile(temp_file_);
  ResetTempFileHandle();
}

bool PluginStream::TempFileIsValid() const {
  return temp_file_ != NULL;
}

}  // namespace npapi
}  // namespace webkit
