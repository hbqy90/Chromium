// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_MAIN_DELEGATE_H_
#define CONTENT_SHELL_SHELL_MAIN_DELEGATE_H_
#pragma once

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/shell/shell_content_client.h"
#include "content/public/app/content_main_delegate.h"

namespace content {
class ShellContentBrowserClient;
class ShellContentRendererClient;
class ShellContentPluginClient;
class ShellContentUtilityClient;
}  // namespace content

class ShellMainDelegate : public content::ContentMainDelegate {
 public:
  ShellMainDelegate();
  virtual ~ShellMainDelegate();

  virtual bool BasicStartupComplete(int* exit_code) OVERRIDE;
  virtual void PreSandboxStartup() OVERRIDE;
  virtual int RunProcess(
      const std::string& process_type,
      const content::MainFunctionParams& main_function_params) OVERRIDE;
#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)
  virtual void ZygoteForked() OVERRIDE;
#endif

 private:
  void InitializeShellContentClient(const std::string& process_type);
  void InitializeResourceBundle();

  scoped_ptr<content::ShellContentBrowserClient> browser_client_;
  scoped_ptr<content::ShellContentRendererClient> renderer_client_;
  scoped_ptr<content::ShellContentPluginClient> plugin_client_;
  scoped_ptr<content::ShellContentUtilityClient> utility_client_;
  content::ShellContentClient content_client_;

  DISALLOW_COPY_AND_ASSIGN(ShellMainDelegate);
};

#endif  // CONTENT_SHELL_SHELL_MAIN_DELEGATE_H_
