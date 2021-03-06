// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_GLOBAL_ERROR_BUBBLE_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_GLOBAL_ERROR_BUBBLE_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#import "chrome/browser/ui/cocoa/base_bubble_controller.h"

class Browser;
class GlobalError;
@class GTMUILocalizerAndLayoutTweaker;
@class GTMWidthBasedTweaker;
class Profile;

namespace GlobalErrorBubbleControllerInternal {
class Bridge;
}

// This is a bubble view shown from the wrench menu to display information
// about a global error.
@interface GlobalErrorBubbleController : BaseBubbleController {
 @private
  base::WeakPtr<GlobalError> error_;
  scoped_ptr<GlobalErrorBubbleControllerInternal::Bridge> bridge_;
  Browser* browser_;

  IBOutlet NSImageView* iconView_;
  IBOutlet NSTextField* title_;
  IBOutlet NSTextField* message_;
  IBOutlet NSButton* acceptButton_;
  IBOutlet NSButton* cancelButton_;
  IBOutlet GTMUILocalizerAndLayoutTweaker* layoutTweaker_;
  IBOutlet GTMWidthBasedTweaker* buttonContainer_;
}

- (IBAction)onAccept:(id)sender;
- (IBAction)onCancel:(id)sender;

- (void)close;

@end

#endif  // CHROME_BROWSER_UI_COCOA_GLOBAL_ERROR_BUBBLE_CONTROLLER_H_
