// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/icon_loader.h"

#include <map>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop.h"
#include "chrome/browser/icon_loader.h"
#include "grit/component_extension_resources.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "webkit/glue/image_decoder.h"

namespace {

// Used with GenerateBitmapWithSize() to indicate that the image shouldn't be
// resized.
const int kDoNotResize = -1;

struct IdrBySize {
  int idr_small;
  int idr_normal;
  int idr_large;
};

// Performs mapping of <file extension, icon size> to icon resource IDs.
class IconMapper {
 public:
  IconMapper();

  // Lookup icon resource ID for a given filename |extension| and
  // |icon_size|. Defaults to generic icons if there are no icons for the given
  // extension.
  int Lookup(const std::string& extension, IconLoader::IconSize icon_size);

 private:
  typedef std::map<std::string, IdrBySize> ExtensionIconMap;

  ExtensionIconMap extension_icon_map_;
};

const IdrBySize kAudioIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_AUDIO,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_AUDIO,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_AUDIO
};
const IdrBySize kGenericIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_GENERIC,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_GENERIC,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_GENERIC
};
const IdrBySize kHtmlIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_HTML,
  IDR_FILE_MANAGER_IMG_FILETYPE_HTML,
  IDR_FILE_MANAGER_IMG_FILETYPE_HTML
};
const IdrBySize kImageIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_IMAGE,
  IDR_FILE_MANAGER_IMG_FILETYPE_IMAGE,
  IDR_FILE_MANAGER_IMG_FILETYPE_IMAGE
};
const IdrBySize kPdfIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_PDF,
  IDR_FILE_MANAGER_IMG_FILETYPE_PDF,
  IDR_FILE_MANAGER_IMG_FILETYPE_PDF
};
const IdrBySize kTextIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_TEXT,
  IDR_FILE_MANAGER_IMG_FILETYPE_TEXT,
  IDR_FILE_MANAGER_IMG_FILETYPE_TEXT
};
const IdrBySize kVideoIdrs = {
  IDR_FILE_MANAGER_IMG_FILETYPE_VIDEO,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_VIDEO,
  IDR_FILE_MANAGER_IMG_FILETYPE_LARGE_VIDEO
};

IconMapper::IconMapper() {
  // The code below should match translation in
  // chrome/browser/resources/file_manager/js/file_manager.js
  // chrome/browser/resources/file_manager/css/file_manager.css
  // 'audio': /\.(mp3|m4a|oga|ogg|wav)$/i,
  // 'html': /\.(html?)$/i,
  // 'image': /\.(bmp|gif|jpe?g|ico|png|webp)$/i,
  // 'pdf' : /\.(pdf)$/i,
  // 'text': /\.(pod|rst|txt|log)$/i,
  // 'video': /\.(mov|mp4|m4v|mpe?g4?|ogm|ogv|ogx|webm)$/i

  const ExtensionIconMap::value_type kExtensionIdrBySizeData[] = {
#if defined(GOOGLE_CHROME_BUILD) || defined(USE_PROPRIETARY_CODECS)
    std::make_pair(".m4a", kAudioIdrs),
    std::make_pair(".mp3", kAudioIdrs),
    std::make_pair(".pdf", kPdfIdrs),
    std::make_pair(".3gp", kVideoIdrs),
    std::make_pair(".avi", kVideoIdrs),
    std::make_pair(".m4v", kVideoIdrs),
    std::make_pair(".mov", kVideoIdrs),
    std::make_pair(".mp4", kVideoIdrs),
    std::make_pair(".mpeg", kVideoIdrs),
    std::make_pair(".mpg", kVideoIdrs),
    std::make_pair(".mpeg4", kVideoIdrs),
    std::make_pair(".mpg4", kVideoIdrs),
#endif
    std::make_pair(".flac", kAudioIdrs),
    std::make_pair(".oga", kAudioIdrs),
    std::make_pair(".ogg", kAudioIdrs),
    std::make_pair(".wav", kAudioIdrs),
    std::make_pair(".htm", kHtmlIdrs),
    std::make_pair(".html", kHtmlIdrs),
    std::make_pair(".bmp", kImageIdrs),
    std::make_pair(".gif", kImageIdrs),
    std::make_pair(".ico", kImageIdrs),
    std::make_pair(".jpeg", kImageIdrs),
    std::make_pair(".jpg", kImageIdrs),
    std::make_pair(".png", kImageIdrs),
    std::make_pair(".webp", kImageIdrs),
    std::make_pair(".log", kTextIdrs),
    std::make_pair(".pod", kTextIdrs),
    std::make_pair(".rst", kTextIdrs),
    std::make_pair(".txt", kTextIdrs),
    std::make_pair(".ogm", kVideoIdrs),
    std::make_pair(".ogv", kVideoIdrs),
    std::make_pair(".ogx", kVideoIdrs),
    std::make_pair(".webm", kVideoIdrs),
  };

  const size_t kESize = arraysize(kExtensionIdrBySizeData);
  ExtensionIconMap source(&kExtensionIdrBySizeData[0],
                          &kExtensionIdrBySizeData[kESize]);
  extension_icon_map_.swap(source);
}

int IconMapper::Lookup(const std::string& extension,
                       IconLoader::IconSize icon_size) {
  DCHECK(icon_size == IconLoader::SMALL ||
         icon_size == IconLoader::NORMAL ||
         icon_size == IconLoader::LARGE);
  ExtensionIconMap::const_iterator it = extension_icon_map_.find(extension);
  const IdrBySize& idrbysize =
      ((it == extension_icon_map_.end()) ? kGenericIdrs : it->second);
  int idr = -1;
  switch (icon_size) {
    case IconLoader::SMALL:  idr = idrbysize.idr_small;  break;
    case IconLoader::NORMAL: idr = idrbysize.idr_normal; break;
    case IconLoader::LARGE:  idr = idrbysize.idr_large;  break;
    case IconLoader::ALL:
    default:
      NOTREACHED();
  }
  return idr;
}

// Returns a copy of |source| that is |pixel_size| in width and height. If
// |pixel_size| is |kDoNotResize|, returns an unmodified copy of |source|.
// |source| must be a square image (width == height).
SkBitmap GenerateBitmapWithSize(const SkBitmap& source, int pixel_size) {
  DCHECK(!source.isNull());
  DCHECK(source.width() == source.height());

  if (pixel_size == kDoNotResize || source.width() == pixel_size)
    return source;

  return skia::ImageOperations::Resize(
      source, skia::ImageOperations::RESIZE_BEST, pixel_size, pixel_size);
}

int IconSizeToPixelSize(IconLoader::IconSize size) {
  switch (size) {
    case IconLoader::SMALL:  return 16;
    case IconLoader::NORMAL: return 32;
    case IconLoader::LARGE:  // fallthrough
      // On ChromeOS, we consider LARGE to mean "the largest image we have."
      // Since we have already chosen the largest applicable image resource, we
      // return the image as-is.
    case IconLoader::ALL:    // fallthrough
    default:
      return kDoNotResize;
  }
}

}  // namespace

void IconLoader::ReadIcon() {
  static base::LazyInstance<IconMapper>::Leaky icon_mapper =
      LAZY_INSTANCE_INITIALIZER;
  int idr = icon_mapper.Get().Lookup(group_, icon_size_);
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  scoped_refptr<base::RefCountedStaticMemory> bytes(
      rb.LoadDataResourceBytes(idr, ui::SCALE_FACTOR_100P));
  DCHECK(bytes.get());
  SkBitmap bitmap;
  if (!gfx::PNGCodec::Decode(bytes->front(), bytes->size(), &bitmap))
    NOTREACHED();
  image_.reset(new gfx::Image(
      GenerateBitmapWithSize(bitmap, IconSizeToPixelSize(icon_size_))));
  target_message_loop_->PostTask(
      FROM_HERE, base::Bind(&IconLoader::NotifyDelegate, this));
}
