// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/local_file_util.h"

#include "base/file_util_proxy.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_mount_point_provider.h"
#include "webkit/fileapi/file_system_operation_context.h"
#include "webkit/fileapi/file_system_path.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_util.h"
#include "webkit/fileapi/native_file_util.h"

namespace fileapi {

class LocalFileEnumerator : public FileSystemFileUtil::AbstractFileEnumerator {
 public:
  LocalFileEnumerator(const FilePath& platform_root_path,
                      const FilePath& virtual_root_path,
                      bool recursive,
                      file_util::FileEnumerator::FileType file_type)
      : file_enum_(platform_root_path, recursive, file_type),
        platform_root_path_(platform_root_path),
        virtual_root_path_(virtual_root_path) {
#if defined(OS_WIN)
    memset(&file_util_info_, 0, sizeof(file_util_info_));
#endif  // defined(OS_WIN)
  }

  ~LocalFileEnumerator() {}

  virtual FilePath Next() OVERRIDE;
  virtual int64 Size() OVERRIDE;
  virtual base::Time LastModifiedTime() OVERRIDE;
  virtual bool IsDirectory() OVERRIDE;
  virtual bool IsLink() OVERRIDE;

 private:
  file_util::FileEnumerator file_enum_;
  file_util::FileEnumerator::FindInfo file_util_info_;
  FilePath platform_root_path_;
  FilePath virtual_root_path_;
};

FilePath LocalFileEnumerator::Next() {
  FilePath next = file_enum_.Next();
  if (next.empty())
    return next;
  file_enum_.GetFindInfo(&file_util_info_);

  FilePath path;
  platform_root_path_.AppendRelativePath(next, &path);
  return virtual_root_path_.Append(path);
}

int64 LocalFileEnumerator::Size() {
  return file_util::FileEnumerator::GetFilesize(file_util_info_);
}

base::Time LocalFileEnumerator::LastModifiedTime() {
  return file_util::FileEnumerator::GetLastModifiedTime(file_util_info_);
}

bool LocalFileEnumerator::IsDirectory() {
  return file_util::FileEnumerator::IsDirectory(file_util_info_);
}

bool LocalFileEnumerator::IsLink() {
  return file_util::FileEnumerator::IsLink(file_util_info_);
}

LocalFileUtil::LocalFileUtil() {
}

LocalFileUtil::~LocalFileUtil() {
}

PlatformFileError LocalFileUtil::CreateOrOpen(
    FileSystemOperationContext* context,
    const FileSystemPath& path, int file_flags,
    PlatformFile* file_handle, bool* created) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::CreateOrOpen(
      file_path, file_flags, file_handle, created);
}

PlatformFileError LocalFileUtil::Close(FileSystemOperationContext* context,
                                       PlatformFile file) {
  return NativeFileUtil::Close(file);
}

PlatformFileError LocalFileUtil::EnsureFileExists(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    bool* created) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::EnsureFileExists(file_path, created);
}

PlatformFileError LocalFileUtil::CreateDirectory(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    bool exclusive,
    bool recursive) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::CreateDirectory(file_path, exclusive, recursive);
}

PlatformFileError LocalFileUtil::GetFileInfo(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    base::PlatformFileInfo* file_info,
    FilePath* platform_file_path) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  error = NativeFileUtil::GetFileInfo(file_path, file_info);
  if (error == base::PLATFORM_FILE_OK)
    *platform_file_path = file_path;
  return error;
}

FileSystemFileUtil::AbstractFileEnumerator* LocalFileUtil::CreateFileEnumerator(
    FileSystemOperationContext* context,
    const FileSystemPath& root_path,
    bool recursive) {
  FilePath file_path;
  if (GetLocalFilePath(context, root_path, &file_path) !=
      base::PLATFORM_FILE_OK)
    return new EmptyFileEnumerator();
  return new LocalFileEnumerator(
      file_path, root_path.internal_path(), recursive,
      static_cast<file_util::FileEnumerator::FileType>(
          file_util::FileEnumerator::FILES |
          file_util::FileEnumerator::DIRECTORIES));
}

PlatformFileError LocalFileUtil::GetLocalFilePath(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    FilePath* local_file_path) {

  FileSystemMountPointProvider* provider =
      context->file_system_context()->GetMountPointProvider(path.type());
  DCHECK(provider);
  FilePath root = provider->GetFileSystemRootPathOnFileThread(
      path.origin(), path.type(), path.internal_path(), false);
  if (root.empty())
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  *local_file_path = root.Append(path.internal_path());
  return base::PLATFORM_FILE_OK;
}

PlatformFileError LocalFileUtil::Touch(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::Touch(file_path, last_access_time, last_modified_time);
}

PlatformFileError LocalFileUtil::Truncate(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    int64 length) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::Truncate(file_path, length);
}

bool LocalFileUtil::PathExists(
    FileSystemOperationContext* context,
    const FileSystemPath& path) {
  FilePath file_path;
  if (GetLocalFilePath(context, path, &file_path) != base::PLATFORM_FILE_OK)
    return false;
  return NativeFileUtil::PathExists(file_path);
}

bool LocalFileUtil::DirectoryExists(
    FileSystemOperationContext* context,
    const FileSystemPath& path) {
  FilePath file_path;
  if (GetLocalFilePath(context, path, &file_path) != base::PLATFORM_FILE_OK)
    return false;
  return NativeFileUtil::DirectoryExists(file_path);
}

bool LocalFileUtil::IsDirectoryEmpty(
    FileSystemOperationContext* context,
    const FileSystemPath& path) {
  FilePath file_path;
  if (GetLocalFilePath(context, path, &file_path) != base::PLATFORM_FILE_OK)
    return true;
  return NativeFileUtil::IsDirectoryEmpty(file_path);
}

PlatformFileError LocalFileUtil::CopyOrMoveFile(
    FileSystemOperationContext* context,
    const FileSystemPath& src_path,
    const FileSystemPath& dest_path,
    bool copy) {
  FilePath src_file_path;
  PlatformFileError error = GetLocalFilePath(context, src_path, &src_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  FilePath dest_file_path;
  error = GetLocalFilePath(context, dest_path, &dest_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;

  return NativeFileUtil::CopyOrMoveFile(src_file_path, dest_file_path, copy);
}

// TODO(dmikurube): Make it independent from CopyOrMoveFile.
PlatformFileError LocalFileUtil::CopyInForeignFile(
    FileSystemOperationContext* context,
    const FilePath& src_file_path,
    const FileSystemPath& dest_path) {
  if (src_file_path.empty())
    return base::PLATFORM_FILE_ERROR_INVALID_OPERATION;

  FilePath dest_file_path;
  PlatformFileError error =
      GetLocalFilePath(context, dest_path, &dest_file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::CopyOrMoveFile(src_file_path, dest_file_path, true);
}

PlatformFileError LocalFileUtil::DeleteFile(
    FileSystemOperationContext* context,
    const FileSystemPath& path) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::DeleteFile(file_path);
}

PlatformFileError LocalFileUtil::DeleteSingleDirectory(
    FileSystemOperationContext* context,
    const FileSystemPath& path) {
  FilePath file_path;
  PlatformFileError error = GetLocalFilePath(context, path, &file_path);
  if (error != base::PLATFORM_FILE_OK)
    return error;
  return NativeFileUtil::DeleteSingleDirectory(file_path);
}

}  // namespace fileapi
