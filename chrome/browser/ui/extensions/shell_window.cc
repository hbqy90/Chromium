// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/shell_window.h"

#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_tabs_module_constants.h"
#include "chrome/browser/extensions/extension_window_controller.h"
#include "chrome/browser/extensions/shell_window_registry.h"
#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/intents/web_intents_util.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/intents/web_intent_picker_controller.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/view_type_utils.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_intents_dispatcher.h"
#include "content/public/common/renderer_preferences.h"

using content::SiteInstance;
using content::WebContents;

namespace {
static const int kDefaultWidth = 512;
static const int kDefaultHeight = 384;
}  // namespace

namespace internal {

class ShellWindowController : public ExtensionWindowController {
 public:
  ShellWindowController(ShellWindow* shell_window, Profile* profile);

  // Overriden from ExtensionWindowController
  virtual int GetWindowId() const OVERRIDE;
  virtual std::string GetWindowTypeText() const OVERRIDE;
  virtual base::DictionaryValue* CreateWindowValueWithTabs() const OVERRIDE;
  virtual bool CanClose(Reason* reason) const OVERRIDE;
  virtual void SetFullscreenMode(bool is_fullscreen,
                                 const GURL& extension_url) const OVERRIDE;
  virtual bool IsVisibleToExtension(
      const extensions::Extension* extension) const OVERRIDE;

 private:
  ShellWindow* shell_window_;

  DISALLOW_COPY_AND_ASSIGN(ShellWindowController);
};

ShellWindowController::ShellWindowController(
    ShellWindow* shell_window,
    Profile* profile)
    : ExtensionWindowController(shell_window, profile),
      shell_window_(shell_window) {
}

int ShellWindowController::GetWindowId() const {
  return static_cast<int>(shell_window_->session_id().id());
}

std::string ShellWindowController::GetWindowTypeText() const {
  return extension_tabs_module_constants::kWindowTypeValueShell;
}

base::DictionaryValue* ShellWindowController::CreateWindowValueWithTabs()
    const {
  return CreateWindowValue();
}

bool ShellWindowController::CanClose(Reason* reason) const {
  return true;
}

void ShellWindowController::SetFullscreenMode(bool is_fullscreen,
                                              const GURL& extension_url) const {
  // TODO(mihaip): implement
}

bool ShellWindowController::IsVisibleToExtension(
    const extensions::Extension* extension) const {
  return shell_window_->extension() == extension;
}

}  // namespace internal

ShellWindow::CreateParams::CreateParams()
  : frame(ShellWindow::CreateParams::FRAME_CUSTOM),
    bounds(10, 10, kDefaultWidth, kDefaultHeight) {
}

ShellWindow* ShellWindow::Create(Profile* profile,
                                 const extensions::Extension* extension,
                                 const GURL& url,
                                 const ShellWindow::CreateParams params) {
  // This object will delete itself when the window is closed.
  return ShellWindow::CreateImpl(profile, extension, url, params);
}

ShellWindow::ShellWindow(Profile* profile,
                         const extensions::Extension* extension,
                         const GURL& url)
    : profile_(profile),
      extension_(extension),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          extension_function_dispatcher_(profile, this)) {
  web_contents_ = WebContents::Create(
      profile, SiteInstance::CreateForURL(profile, url), MSG_ROUTING_NONE, NULL,
      NULL);
  contents_wrapper_.reset(new TabContentsWrapper(web_contents_));
  content::WebContentsObserver::Observe(web_contents_);
  web_contents_->SetDelegate(this);
  chrome::SetViewType(web_contents_, chrome::VIEW_TYPE_APP_SHELL);
  web_contents_->GetMutableRendererPrefs()->
      browser_handles_all_top_level_requests = true;
  web_contents_->GetRenderViewHost()->SyncRendererPrefs();

  web_contents_->GetController().LoadURL(
      url, content::Referrer(), content::PAGE_TRANSITION_LINK,
      std::string());
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_));
  // Close when the browser is exiting.
  // TODO(mihaip): we probably don't want this in the long run (when platform
  // apps are no longer tied to the browser process).
  registrar_.Add(this, content::NOTIFICATION_APP_TERMINATING,
                 content::NotificationService::AllSources());

  // Prevent the browser process from shutting down while this window is open.
  browser::StartKeepAlive();

  // Make this window available to the extension API.
  extension_window_controller_.reset(
      new internal::ShellWindowController(this, profile_));

  ShellWindowRegistry::Get(profile_)->AddShellWindow(this);
}

ShellWindow::~ShellWindow() {
  // Unregister now to prevent getting NOTIFICATION_APP_TERMINATING if we're the
  // last window open.
  registrar_.RemoveAll();

  ShellWindowRegistry::Get(profile_)->RemoveShellWindow(this);

  // Remove shutdown prevention.
  browser::EndKeepAlive();
}

bool ShellWindow::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ShellWindow, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_Request, OnRequest)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ShellWindow::CloseContents(WebContents* contents) {
  Close();
}

bool ShellWindow::ShouldSuppressDialogs() {
  return true;
}

void ShellWindow::WebIntentDispatch(
    content::WebContents* web_contents,
    content::WebIntentsDispatcher* intents_dispatcher) {
  if (!web_intents::IsWebIntentsEnabledForProfile(profile_))
    return;

  contents_wrapper_->web_intent_picker_controller()->SetIntentsDispatcher(
      intents_dispatcher);
  contents_wrapper_->web_intent_picker_controller()->ShowDialog(
      intents_dispatcher->GetIntent().action,
      intents_dispatcher->GetIntent().type);
}

void ShellWindow::RunFileChooser(WebContents* tab,
                                 const content::FileChooserParams& params) {
  FileSelectHelper::RunFileChooser(tab, params);
}

bool ShellWindow::IsPopupOrPanel(const WebContents* source) const {
  DCHECK(source == web_contents_);
  return true;
}

void ShellWindow::MoveContents(WebContents* source, const gfx::Rect& pos) {
  DCHECK(source == web_contents_);
  extension_window_controller_->window()->SetBounds(pos);
}

void ShellWindow::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_EXTENSION_UNLOADED: {
      const extensions::Extension* unloaded_extension =
          content::Details<extensions::UnloadedExtensionInfo>(
              details)->extension;
      if (extension_ == unloaded_extension)
        Close();
      break;
    }
    case content::NOTIFICATION_APP_TERMINATING:
      Close();
      break;
    default:
      NOTREACHED() << "Received unexpected notification";
  }
}

ExtensionWindowController* ShellWindow::GetExtensionWindowController() const {
  return extension_window_controller_.get();
}

void ShellWindow::OnRequest(const ExtensionHostMsg_Request_Params& params) {
  extension_function_dispatcher_.Dispatch(params,
                                          web_contents_->GetRenderViewHost());
}
