// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_CAPTURING_NET_LOG_H_
#define NET_BASE_CAPTURING_NET_LOG_H_
#pragma once

#include <string>
#include <vector>

#include "base/atomicops.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/time.h"
#include "net/base/net_export.h"
#include "net/base/net_log.h"

namespace base {
class DictionaryValue;
}

namespace net {

// CapturingNetLog is an implementation of NetLog that saves messages to a
// bounded buffer.  CapturingNetLogs should only be used in unit tests, never
// in production code.
// TODO(mmenke):  Move CapturingNetLog to net_unittests project, once the CL
//                to remove the use of it in the Jingle unit tests has landed.
class NET_EXPORT CapturingNetLog : public NetLog {
 public:
  struct NET_EXPORT_PRIVATE CapturedEntry {
    CapturedEntry(EventType type,
                  const base::TimeTicks& time,
                  Source source,
                  EventPhase phase,
                  scoped_ptr<base::DictionaryValue> params);
    // Copy constructor needed to store in a std::vector because of the
    // scoped_ptr.
    CapturedEntry(const CapturedEntry& entry);

    ~CapturedEntry();

    // Equality operator needed to store in a std::vector because of the
    // scoped_ptr.
    CapturedEntry& operator=(const CapturedEntry& entry);

    // Attempt to retrieve an value of the specified type with the given name
    // from |params|.  Returns true on success, false on failure.  Does not
    // modify |value| on failure.
    bool GetStringValue(const std::string& name, std::string* value) const;
    bool GetIntegerValue(const std::string& name, int* value) const;

    // Same as GetIntegerValue, but returns the error code associated with a
    // log entry.
    bool GetNetErrorCode(int* value) const;

    EventType type;
    base::TimeTicks time;
    Source source;
    EventPhase phase;
    scoped_ptr<base::DictionaryValue> params;
  };

  // Ordered set of entries that were logged.
  typedef std::vector<CapturedEntry> CapturedEntryList;

  enum { kUnbounded = -1 };

  // Creates a CapturingNetLog that logs a maximum of |max_num_entries|
  // messages.
  explicit CapturingNetLog(size_t max_num_entries);
  virtual ~CapturingNetLog();

  // Returns the list of all entries in the log.
  void GetEntries(CapturedEntryList* entry_list) const;

  void Clear();

  void SetLogLevel(NetLog::LogLevel log_level);

  // NetLog implementation:
  virtual void AddEntry(
      EventType type,
      const Source& source,
      EventPhase phase,
      const scoped_refptr<EventParameters>& extra_parameters) OVERRIDE;
  virtual uint32 NextID() OVERRIDE;
  virtual LogLevel GetLogLevel() const OVERRIDE;
  virtual void AddThreadSafeObserver(ThreadSafeObserver* observer,
                                     LogLevel log_level) OVERRIDE;
  virtual void SetObserverLogLevel(ThreadSafeObserver* observer,
                                   LogLevel log_level) OVERRIDE;
  virtual void RemoveThreadSafeObserver(ThreadSafeObserver* observer) OVERRIDE;

 private:
  // Needs to be "mutable" so can use it in GetEntries().
  mutable base::Lock lock_;

  // Last assigned source ID.  Incremented to get the next one.
  base::subtle::Atomic32 last_id_;

  size_t max_num_entries_;
  CapturedEntryList captured_entries_;

  NetLog::LogLevel log_level_;

  DISALLOW_COPY_AND_ASSIGN(CapturingNetLog);
};

// Helper class that exposes a similar API as BoundNetLog, but uses a
// CapturingNetLog rather than the more generic NetLog.
//
// CapturingBoundNetLog can easily be converted to a BoundNetLog using the
// bound() method.
class NET_EXPORT_PRIVATE CapturingBoundNetLog {
 public:
  explicit CapturingBoundNetLog(size_t max_num_entries);

  ~CapturingBoundNetLog();

  // The returned BoundNetLog is only valid while |this| is alive.
  BoundNetLog bound() const { return net_log_; }

  // Fills |entry_list| with all entries in the log.
  void GetEntries(CapturingNetLog::CapturedEntryList* entry_list) const;

  void Clear();

  // Sets the log level of the underlying CapturingNetLog.
  void SetLogLevel(NetLog::LogLevel log_level);

 private:
  CapturingNetLog capturing_net_log_;
  const BoundNetLog net_log_;

  DISALLOW_COPY_AND_ASSIGN(CapturingBoundNetLog);
};

}  // namespace net

#endif  // NET_BASE_CAPTURING_NET_LOG_H_
