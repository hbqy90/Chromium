// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/test/views_test_base.h"

#if defined(USE_AURA)
#include "ui/aura/env.h"
#include "ui/aura/test/aura_test_helper.h"
#endif

namespace views {

ViewsTestBase::ViewsTestBase()
    : setup_called_(false),
      teardown_called_(false) {
}

ViewsTestBase::~ViewsTestBase() {
  CHECK(setup_called_)
      << "You have overridden SetUp but never called super class's SetUp";
  CHECK(teardown_called_)
      << "You have overrideen TearDown but never called super class's TearDown";
}

void ViewsTestBase::SetUp() {
  testing::Test::SetUp();
  setup_called_ = true;
  if (!views_delegate_.get())
    views_delegate_.reset(new TestViewsDelegate());
#if defined(USE_AURA)
  aura_test_helper_.reset(new aura::test::AuraTestHelper(&message_loop_));
  aura_test_helper_->SetUp();
#endif  // USE_AURA
}

void ViewsTestBase::TearDown() {
  // Flush the message loop because we have pending release tasks
  // and these tasks if un-executed would upset Valgrind.
  RunPendingMessages();
  teardown_called_ = true;
  views_delegate_.reset();
  testing::Test::TearDown();
#if defined(USE_AURA)
  aura_test_helper_->TearDown();
#endif
}

void ViewsTestBase::RunPendingMessages() {
#if defined(USE_AURA)
  message_loop_.RunAllPendingWithDispatcher(
      aura::Env::GetInstance()->GetDispatcher());
#else
  message_loop_.RunAllPending();
#endif
}

}  // namespace views
