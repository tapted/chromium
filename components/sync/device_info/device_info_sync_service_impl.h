// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DEVICE_INFO_DEVICE_INFO_SYNC_SERVICE_IMPL_H_
#define COMPONENTS_SYNC_DEVICE_INFO_DEVICE_INFO_SYNC_SERVICE_IMPL_H_

#include <memory>
#include <string>

#include "components/sync/device_info/device_info_sync_service.h"
#include "components/sync/model/model_type_store.h"

namespace syncer {

class DeviceInfoSyncBridge;
class MutableLocalDeviceInfoProvider;

class DeviceInfoSyncServiceImpl : public DeviceInfoSyncService {
 public:
  // |local_device_info_provider| must not be null.
  DeviceInfoSyncServiceImpl(OnceModelTypeStoreFactory model_type_store_factory,
                            std::unique_ptr<MutableLocalDeviceInfoProvider>
                                local_device_info_provider);
  ~DeviceInfoSyncServiceImpl() override;

  // DeviceInfoSyncService implementation.
  LocalDeviceInfoProvider* GetLocalDeviceInfoProvider() override;
  DeviceInfoTracker* GetDeviceInfoTracker() override;
  base::WeakPtr<ModelTypeControllerDelegate> GetControllerDelegate() override;

 private:
  std::unique_ptr<DeviceInfoSyncBridge> bridge_;

  DISALLOW_COPY_AND_ASSIGN(DeviceInfoSyncServiceImpl);
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DEVICE_INFO_DEVICE_INFO_SYNC_SERVICE_IMPL_H_
