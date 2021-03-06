// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_MOCK_DEVICE_MANAGEMENT_SERVICE_H_
#define CHROME_BROWSER_POLICY_MOCK_DEVICE_MANAGEMENT_SERVICE_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/policy/device_management_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace policy {

class MockDeviceManagementService : public DeviceManagementService {
 public:
  MockDeviceManagementService();
  virtual ~MockDeviceManagementService();

  typedef DeviceManagementRequestJob* CreateJobFunction(
      DeviceManagementRequestJob::JobType);

  MOCK_METHOD1(CreateJob, CreateJobFunction);
  MOCK_METHOD7(
      StartJob,
      void(const std::string& request_type,
           const std::string& gaia_token,
           const std::string& oauth_token,
           const std::string& dm_token,
           const std::string& user_affiliation,
           const std::string& client_id,
           const enterprise_management::DeviceManagementRequest& request));

  // Creates a gmock action that will make the job succeed.
  testing::Action<CreateJobFunction> SucceedJob(
      const enterprise_management::DeviceManagementResponse& response);
  // Creates a gmock action which will fail the job with the given error.
  testing::Action<CreateJobFunction> FailJob(DeviceManagementStatus status);

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDeviceManagementService);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_MOCK_DEVICE_MANAGEMENT_SERVICE_H_
