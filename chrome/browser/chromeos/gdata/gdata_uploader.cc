// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_uploader.h"

#include <algorithm>

#include "base/bind.h"
#include "base/callback.h"
#include "chrome/browser/chromeos/gdata/gdata_documents_service.h"
#include "chrome/browser/chromeos/gdata/gdata_files.h"
#include "chrome/browser/chromeos/gdata/gdata_file_system.h"
#include "chrome/browser/chromeos/gdata/gdata_upload_file_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item.h"
#include "net/base/file_stream.h"
#include "net/base/net_errors.h"

using content::BrowserThread;

namespace {

// Google Documents List API requires uploading in chunks of 512kB.
const int64 kUploadChunkSize = 512 * 1024;

// Maximum number of times we try to open a file before giving up.
const int kMaxFileOpenTries = 5;

}  // namespace

namespace gdata {

GDataUploader::GDataUploader(GDataFileSystem* file_system,
                             DocumentsServiceInterface* documents_service)
  : file_system_(file_system),
    documents_service_(documents_service),
    next_upload_id_(0),
    ALLOW_THIS_IN_INITIALIZER_LIST(uploader_factory_(this)) {
}

GDataUploader::~GDataUploader() {
}

int GDataUploader::UploadFile(scoped_ptr<UploadFileInfo> upload_file_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(upload_file_info.get());
  DCHECK_EQ(upload_file_info->upload_id, -1);
  DCHECK(!upload_file_info->file_path.empty());
  DCHECK_NE(upload_file_info->file_size, 0);
  DCHECK(!upload_file_info->gdata_path.empty());
  DCHECK(!upload_file_info->title.empty());
  DCHECK(!upload_file_info->content_type.empty());

  const int upload_id = next_upload_id_++;
  upload_file_info->upload_id = upload_id;
  // Add upload_file_info to our internal map and take ownership.
  pending_uploads_[upload_id] = upload_file_info.release();

  UploadFileInfo* info = GetUploadFileInfo(upload_id);
  DVLOG(1) << "Uploading file: " << info->DebugString();

  // Create a FileStream to make sure the file can be opened successfully.
  info->file_stream = new net::FileStream(NULL);

  // Create buffer to hold upload data.
  info->buf_len = std::min(info->file_size, kUploadChunkSize);
  info->buf = new net::IOBuffer(info->buf_len);

  OpenFile(info);
  return upload_id;
}

void GDataUploader::UpdateUpload(int upload_id,
                                 content::DownloadItem* download) {
  UploadFileInfo* upload_file_info = GetUploadFileInfo(upload_id);
  if (!upload_file_info)
    return;

  const int64 file_size = download->GetReceivedBytes();

  // Update file_size and all_bytes_present.
  DVLOG(1) << "Updating file size from " << upload_file_info->file_size
           << " to " << file_size
           << (download->AllDataSaved() ? " (AllDataSaved)" : " (In-progress)");
  upload_file_info->file_size = file_size;
  upload_file_info->all_bytes_present = download->AllDataSaved();
  if (upload_file_info->file_path != download->GetFullPath()) {
    // We shouldn't see a rename if should_retry_file_open is true. The only
    // rename we expect (for now) is the final rename that happens after the
    // download transition from IN_PROGRESS -> COMPLETE. This, in turn, only
    // happens after the upload completes. However, since this isn't enforced by
    // the API contract, we reset the retry count so we can retry all over again
    // with the new path.
    // TODO(asanka): Introduce a synchronization point after the initial rename
    //               of the download and get rid of the retry logic.
    upload_file_info->num_file_open_tries = 0;
    upload_file_info->file_path = download->GetFullPath();
  }

  // Resume upload if necessary and possible.
  if (upload_file_info->upload_paused &&
      (upload_file_info->all_bytes_present ||
       upload_file_info->SizeRemaining() > kUploadChunkSize)) {
    DVLOG(1) << "Resuming upload " << upload_file_info->title;
    upload_file_info->upload_paused = false;
    UploadNextChunk(upload_file_info);
  }

  // Retry opening this file if we failed before.  File open can fail because
  // the downloads system sets the full path on the UI thread and schedules a
  // rename on the FILE thread. Thus the new path is visible on the UI thread
  // before the renamed file is available on the file system.
  if (upload_file_info->should_retry_file_open) {
    DCHECK(!download->IsComplete());
    // Disallow further retries.
    upload_file_info->should_retry_file_open = false;
    OpenFile(upload_file_info);
  }

  if (download->IsComplete())
    MoveFileToCache(upload_file_info);
}

int64 GDataUploader::GetUploadedBytes(int upload_id) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  UploadFileInfo* upload_info = GetUploadFileInfo(upload_id);
  // We return the start_range as the count of uploaded bytes since that is the
  // start of the next or currently uploading chunk.
  // TODO(asanka): Use a finer grained progress value than this. We end up
  //               reporting progress in kUploadChunkSize increments.
  return upload_info ? upload_info->start_range : 0;
}

UploadFileInfo* GDataUploader::GetUploadFileInfo(int upload_id) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  UploadFileInfoMap::const_iterator it = pending_uploads_.find(upload_id);
  DVLOG_IF(1, it == pending_uploads_.end()) << "No upload found for id "
                                            << upload_id;
  return it != pending_uploads_.end() ? it->second : NULL;
}

void GDataUploader::OpenFile(UploadFileInfo* upload_file_info) {
  // Open the file asynchronously.
  const int rv = upload_file_info->file_stream->Open(
      upload_file_info->file_path,
      base::PLATFORM_FILE_OPEN |
      base::PLATFORM_FILE_READ |
      base::PLATFORM_FILE_ASYNC,
      base::Bind(&GDataUploader::OpenCompletionCallback,
                 uploader_factory_.GetWeakPtr(),
                 upload_file_info->upload_id));
  DCHECK_EQ(net::ERR_IO_PENDING, rv);
}

void GDataUploader::OpenCompletionCallback(int upload_id, int result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  UploadFileInfo* upload_file_info = GetUploadFileInfo(upload_id);
  if (!upload_file_info)
    return;

  // The file may actually not exist yet, as the downloads system downloads
  // to a temp location and then renames the file. If this is the case, we
  // just retry opening the file later.
  if (result != net::OK) {
    DCHECK_EQ(result, net::ERR_FILE_NOT_FOUND);
    // File open failed. Try again later.
    upload_file_info->num_file_open_tries++;

    DVLOG(1) << "Error opening \"" << upload_file_info->file_path.value()
             << "\" for reading: " << net::ErrorToString(result)
             << ", tries=" << upload_file_info->num_file_open_tries;

    // Stop trying to open this file if we exceed kMaxFileOpenTries.
    const bool exceeded_max_attempts =
        upload_file_info->num_file_open_tries >= kMaxFileOpenTries;
    upload_file_info->should_retry_file_open = !exceeded_max_attempts;
    if (exceeded_max_attempts)
      UploadFailed(upload_file_info, base::PLATFORM_FILE_ERROR_NOT_FOUND);

    return;
  }

  // Open succeeded, initiate the upload.
  upload_file_info->should_retry_file_open = false;
  const GURL destination_directory_url = file_system_->GetUploadUrlForDirectory(
      upload_file_info->gdata_path.DirName());
  if (destination_directory_url.is_empty()) {
    MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&GDataUploader::OnUploadLocationReceived,
                   uploader_factory_.GetWeakPtr(),
                   upload_file_info->upload_id,
                   HTTP_BAD_REQUEST,
                   GURL()));
    return;
  }

  documents_service_->InitiateUpload(
      InitiateUploadParams(upload_file_info->title,
                           upload_file_info->content_type,
                           upload_file_info->content_length,
                           destination_directory_url,
                           upload_file_info->gdata_path),
      base::Bind(&GDataUploader::OnUploadLocationReceived,
                 uploader_factory_.GetWeakPtr(),
                 upload_file_info->upload_id));
}

void GDataUploader::OnUploadLocationReceived(
    int upload_id,
    GDataErrorCode code,
    const GURL& upload_location) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  UploadFileInfo* upload_file_info = GetUploadFileInfo(upload_id);
  if (!upload_file_info)
    return;

  DVLOG(1) << "Got upload location [" << upload_location.spec()
           << "] for [" << upload_file_info->title << "]";

  if (code != HTTP_SUCCESS) {
    // TODO(achuith): Handle error codes from Google Docs server.
    UploadFailed(upload_file_info, base::PLATFORM_FILE_ERROR_ABORT);
    return;
  }

  upload_file_info->upload_location = upload_location;

  // Start the upload from the beginning of the file.
  UploadNextChunk(upload_file_info);
}

void GDataUploader::UploadNextChunk(UploadFileInfo* upload_file_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // Check that |upload_file_info| is in pending_uploads_.
  DCHECK(upload_file_info == GetUploadFileInfo(upload_file_info->upload_id));
  DVLOG(1) << "Number of pending uploads=" << pending_uploads_.size();

  // Determine number of bytes to read for this upload iteration, which cannot
  // exceed size of buf i.e. buf_len.
  const int64 bytes_remaining = upload_file_info->SizeRemaining();
  const int bytes_to_read = std::min(upload_file_info->SizeRemaining(),
                                     upload_file_info->buf_len);

  // Update the content length if the file_size is known.
  if (upload_file_info->all_bytes_present)
    upload_file_info->content_length = upload_file_info->file_size;
  else if (bytes_remaining == bytes_to_read) {
    // Wait for more data if this is the last chunk we have and we don't know
    // whether we've reached the end of the file. We won't know how much data to
    // expect until the transfer is complete (the Content-Length might be
    // incorrect or absent). If we've sent the last chunk out already when we
    // find out there's no more data, we won't be able to complete the upload.
    DVLOG(1) << "Paused upload " << upload_file_info->title;
    upload_file_info->upload_paused = true;
    return;
  }

  upload_file_info->file_stream->Read(
      upload_file_info->buf,
      bytes_to_read,
      base::Bind(&GDataUploader::ReadCompletionCallback,
                 uploader_factory_.GetWeakPtr(),
                 upload_file_info->upload_id,
                 bytes_to_read));
}

void GDataUploader::ReadCompletionCallback(
    int upload_id,
    int bytes_to_read,
    int bytes_read) {
  // The Read is asynchronously executed on BrowserThread::UI, where
  // Read() was called.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DVLOG(1) << "ReadCompletionCallback bytes read=" << bytes_read;

  UploadFileInfo* upload_file_info = GetUploadFileInfo(upload_id);
  if (!upload_file_info)
    return;

  // TODO(achuith): Handle this error.
  DCHECK_EQ(bytes_to_read, bytes_read);
  DCHECK_GT(bytes_read, 0) << "Error reading from file "
                           << upload_file_info->file_path.value();

  upload_file_info->start_range = upload_file_info->end_range + 1;
  upload_file_info->end_range = upload_file_info->start_range +
                                bytes_read - 1;

  documents_service_->ResumeUpload(
      ResumeUploadParams(upload_file_info->title,
                         upload_file_info->start_range,
                         upload_file_info->end_range,
                         upload_file_info->content_length,
                         upload_file_info->content_type,
                         upload_file_info->buf,
                         upload_file_info->upload_location,
                         upload_file_info->gdata_path),
      base::Bind(&GDataUploader::OnResumeUploadResponseReceived,
                 uploader_factory_.GetWeakPtr(),
                 upload_file_info->upload_id));
}

void GDataUploader::OnResumeUploadResponseReceived(
    int upload_id,
    const ResumeUploadResponse& response,
    scoped_ptr<DocumentEntry> entry) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  UploadFileInfo* upload_file_info = GetUploadFileInfo(upload_id);
  if (!upload_file_info)
    return;

  if (response.code == HTTP_CREATED) {
    DVLOG(1) << "Successfully created uploaded file=["
             << upload_file_info->title;

    // Done uploading.
    upload_file_info->entry = entry.Pass();
    if (!upload_file_info->completion_callback.is_null()) {
      upload_file_info->completion_callback.Run(base::PLATFORM_FILE_OK,
                                                upload_file_info);
    }
    // TODO(achuith): DeleteUpload() here and let clients call
    // GDataFileSystem::AddUploadedFile.
    return;
  }

  // If code is 308 (RESUME_INCOMPLETE) and range_received is what has been
  // previously uploaded (i.e. = upload_file_info->end_range), proceed to
  // upload the next chunk.
  if (response.code != HTTP_RESUME_INCOMPLETE ||
      response.start_range_received != 0 ||
      response.end_range_received != upload_file_info->end_range) {
    // TODO(achuith): Handle error cases, e.g.
    // - when previously uploaded data wasn't received by Google Docs server,
    //   i.e. when end_range_received < upload_file_info->end_range
    LOG(ERROR) << "UploadNextChunk http code=" << response.code
               << ", start_range_received=" << response.start_range_received
               << ", end_range_received=" << response.end_range_received
               << ", expected end range=" << upload_file_info->end_range;
    UploadFailed(upload_file_info,
        response.code == HTTP_FORBIDDEN ?
            base::PLATFORM_FILE_ERROR_NO_SPACE :
            base::PLATFORM_FILE_ERROR_ABORT);
    return;
  }

  DVLOG(1) << "Received range " << response.start_range_received
           << "-" << response.end_range_received
           << " for [" << upload_file_info->title << "]";

  // Continue uploading.
  UploadNextChunk(upload_file_info);
}

void GDataUploader::MoveFileToCache(UploadFileInfo* upload_file_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (upload_file_info->entry == NULL)
    return;

  DVLOG(1) << "MoveFileToCache " << upload_file_info->file_path.value();
  file_system_->AddUploadedFile(
      upload_file_info->gdata_path.DirName(),
      upload_file_info->entry.get(),
      upload_file_info->file_path,
      GDataFileSystemInterface::FILE_OPERATION_MOVE);
  DeleteUpload(upload_file_info);
}

void GDataUploader::UploadFailed(UploadFileInfo* upload_file_info,
                                 base::PlatformFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LOG(ERROR) << "Upload failed " << upload_file_info->DebugString();
  if (!upload_file_info->completion_callback.is_null()) {
    upload_file_info->completion_callback.Run(error,
                                              upload_file_info);
  }
  DeleteUpload(upload_file_info);
}

void GDataUploader::DeleteUpload(UploadFileInfo* upload_file_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DVLOG(1) << "Deleting upload " << upload_file_info->gdata_path.value();
  pending_uploads_.erase(upload_file_info->upload_id);

  // The file stream is closed by the destructor asynchronously.
  delete upload_file_info->file_stream;
  delete upload_file_info;
}

}  // namespace gdata
