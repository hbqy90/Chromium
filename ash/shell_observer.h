// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_OBSERVER_H_
#define ASH_SHELL_OBSERVER_H_
#pragma once

#include "ash/ash_export.h"
#include "ash/system/user/login_status.h"

namespace ash {

class ASH_EXPORT ShellObserver {
 public:
  // Invoked after the screen's work area insets changes.
  virtual void OnMonitorWorkAreaInsetsChanged() {}

  // Invoked when the user logs in.
  virtual void OnLoginStateChanged(user::LoginStatus status) {}

  // Invoked when the application is exiting.
  virtual void OnAppTerminating() {}

  // Invoked when the screen is locked (after the lock window is visible) or
  // unlocked.
  virtual void OnLockStateChanged(bool locked) {}

  // Invoked when the shelf alignment is changed.
  virtual void OnShelfAlignmentChanged() {}

 protected:
  virtual ~ShellObserver() {}
};

}  // namespace ash

#endif  // ASH_SHELL_OBSERVER_H_
