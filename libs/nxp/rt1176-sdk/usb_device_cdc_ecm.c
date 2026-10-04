/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// clang-format off
#include "third_party/modified/nxp/rt1176-sdk/usb_device_config.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/device/usb_device.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/include/usb.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/output/source/device/class/usb_device_class.h"
// clang-format on

#if USB_DEVICE_CONFIG_CDC_ECM
#include "libs/nxp/rt1176-sdk/usb_device_cdc_ecm.h"

USB_GLOBAL USB_RAM_ADDRESS_ALIGNMENT(
    USB_DATA_ALIGN_SIZE) static usb_device_cdc_ecm_struct_t
    g_cdcEcmHandle[USB_DEVICE_CONFIG_CDC_ECM];

static usb_status_t USB_DeviceCdcEcmAllocateHandle(
    usb_device_cdc_ecm_struct_t **handle) {
  uint32_t count;
  for (count = 0; count < USB_DEVICE_CONFIG_CDC_ECM; ++count) {
    if (g_cdcEcmHandle[count].handle == NULL) {
      *handle = &g_cdcEcmHandle[count];
      return kStatus_USB_Success;
    }
  }
  return kStatus_USB_Busy;
}

static usb_status_t USB_DeviceCdcEcmPipeDone(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle, usb_device_cdc_ecm_pipe_t *pipe,
    uint32_t event, usb_device_endpoint_callback_message_struct_t *message) {
  if (!cdcEcmHandle) {
    return kStatus_USB_InvalidHandle;
  }
  pipe->isBusy = 0;
  if (cdcEcmHandle->configStruct && cdcEcmHandle->configStruct->classCallback) {
    return cdcEcmHandle->configStruct->classCallback(
        (class_handle_t)cdcEcmHandle, event, message);
  }
  return kStatus_USB_Error;
}

static usb_status_t USB_DeviceCdcEcmInterruptIn(
    usb_device_handle handle,
    usb_device_endpoint_callback_message_struct_t *message,
    void *callbackParam) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)callbackParam;
  return USB_DeviceCdcEcmPipeDone(cdcEcmHandle, &cdcEcmHandle->interruptIn,
                                  kUSB_DeviceEcmEventNotifyResponse, message);
}

static usb_status_t USB_DeviceCdcEcmBulkIn(
    usb_device_handle handle,
    usb_device_endpoint_callback_message_struct_t *message,
    void *callbackParam) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)callbackParam;
  return USB_DeviceCdcEcmPipeDone(cdcEcmHandle, &cdcEcmHandle->bulkIn,
                                  kUSB_DeviceEcmEventSendResponse, message);
}

static usb_status_t USB_DeviceCdcEcmBulkOut(
    usb_device_handle handle,
    usb_device_endpoint_callback_message_struct_t *message,
    void *callbackParam) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)callbackParam;
  return USB_DeviceCdcEcmPipeDone(cdcEcmHandle, &cdcEcmHandle->bulkOut,
                                  kUSB_DeviceEcmEventRecvResponse, message);
}

static usb_status_t USB_DeviceCdcEcmEndpointsDeinit(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle,
    usb_device_interface_struct_t **interface) {
  usb_status_t error = kStatus_USB_Success;
  uint32_t count;

  if (*interface == NULL) {
    return error;
  }
  for (count = 0; count < (*interface)->endpointList.count; ++count) {
    error = USB_DeviceDeinitEndpoint(
        cdcEcmHandle->handle,
        (*interface)->endpointList.endpoint[count].endpointAddress);
  }
  *interface = NULL;
  return error;
}

static usb_status_t USB_DeviceCdcEcmEndpointsInit(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle, uint8_t classCode,
    uint8_t alternate, usb_device_interface_struct_t **interfaceHandle,
    uint8_t *interfaceNumber) {
  usb_device_interface_list_t *interfaceList;
  usb_device_interface_struct_t *interface = NULL;
  usb_status_t error = kStatus_USB_Success;
  uint32_t count;
  uint32_t index;

  if ((cdcEcmHandle->configuration == 0) ||
      (cdcEcmHandle->configuration >
       cdcEcmHandle->configStruct->classInfomation->configurations)) {
    return kStatus_USB_Error;
  }

  interfaceList = &cdcEcmHandle->configStruct->classInfomation
                       ->interfaceList[cdcEcmHandle->configuration - 1];
  for (count = 0; count < interfaceList->count; ++count) {
    if (interfaceList->interfaces[count].classCode != classCode) {
      continue;
    }
    for (index = 0; index < interfaceList->interfaces[count].count; index++) {
      if (interfaceList->interfaces[count].interface[index].alternateSetting ==
          alternate) {
        interface = &interfaceList->interfaces[count].interface[index];
        break;
      }
    }
    *interfaceNumber = interfaceList->interfaces[count].interfaceNumber;
    break;
  }
  if (interface == NULL) {
    return kStatus_USB_Error;
  }
  *interfaceHandle = interface;

  for (count = 0; count < interface->endpointList.count; ++count) {
    usb_device_endpoint_struct_t *endpoint =
        &interface->endpointList.endpoint[count];
    usb_device_endpoint_init_struct_t epInitStruct;
    usb_device_endpoint_callback_struct_t epCallback;
    usb_device_cdc_ecm_pipe_t *pipe;
    uint8_t in = ((endpoint->endpointAddress &
                   USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) >>
                  USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT) == USB_IN;

    epInitStruct.zlt = 0;
    epInitStruct.interval = endpoint->interval;
    epInitStruct.endpointAddress = endpoint->endpointAddress;
    epInitStruct.maxPacketSize = endpoint->maxPacketSize;
    epInitStruct.transferType = endpoint->transferType;

    if (endpoint->transferType == USB_ENDPOINT_INTERRUPT) {
      pipe = &cdcEcmHandle->interruptIn;
      epCallback.callbackFn = USB_DeviceCdcEcmInterruptIn;
    } else if (in) {
      pipe = &cdcEcmHandle->bulkIn;
      epCallback.callbackFn = USB_DeviceCdcEcmBulkIn;
      // A frame that fills its last packet needs a ZLP to end the transfer.
      epInitStruct.zlt = 1;
    } else {
      pipe = &cdcEcmHandle->bulkOut;
      epCallback.callbackFn = USB_DeviceCdcEcmBulkOut;
    }
    pipe->ep = endpoint->endpointAddress;
    pipe->isBusy = 0;
    epCallback.callbackParam = cdcEcmHandle;
    error = USB_DeviceInitEndpoint(cdcEcmHandle->handle, &epInitStruct,
                                   &epCallback);
  }

  return error;
}

static usb_status_t USB_DeviceCdcEcmDataEndpointsInit(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle, uint8_t alternate) {
  (void)USB_DeviceCdcEcmEndpointsDeinit(cdcEcmHandle,
                                        &cdcEcmHandle->dataInterfaceHandle);
  cdcEcmHandle->dataAlternate = alternate;
  return USB_DeviceCdcEcmEndpointsInit(
      cdcEcmHandle, USB_DEVICE_CDC_ECM_DATA_CLASS_CODE, alternate,
      &cdcEcmHandle->dataInterfaceHandle, &cdcEcmHandle->dataInterfaceNumber);
}

// Returns the pipe that owns |endpointAddress|, or NULL.
static usb_device_cdc_ecm_pipe_t *USB_DeviceCdcEcmFindPipe(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle, uint8_t endpointAddress) {
  if (cdcEcmHandle->commInterfaceHandle != NULL &&
      cdcEcmHandle->interruptIn.ep == endpointAddress) {
    return &cdcEcmHandle->interruptIn;
  }
  if (cdcEcmHandle->dataInterfaceHandle != NULL &&
      cdcEcmHandle->dataInterfaceHandle->endpointList.count != 0) {
    if (cdcEcmHandle->bulkIn.ep == endpointAddress) {
      return &cdcEcmHandle->bulkIn;
    }
    if (cdcEcmHandle->bulkOut.ep == endpointAddress) {
      return &cdcEcmHandle->bulkOut;
    }
  }
  return NULL;
}

usb_status_t USB_DeviceCdcEcmInit(uint8_t controllerId,
                                  usb_device_class_config_struct_t *config,
                                  class_handle_t *handle) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle;
  usb_status_t error;

  error = USB_DeviceCdcEcmAllocateHandle(&cdcEcmHandle);
  if (error != kStatus_USB_Success) {
    return error;
  }

  error = USB_DeviceClassGetDeviceHandle(controllerId, &cdcEcmHandle->handle);
  if (error != kStatus_USB_Success) {
    return error;
  }

  if (NULL == cdcEcmHandle->handle) {
    return kStatus_USB_InvalidHandle;
  }

  cdcEcmHandle->configStruct = config;
  cdcEcmHandle->configuration = 0;
  cdcEcmHandle->dataAlternate = 0;
  cdcEcmHandle->commInterfaceHandle = NULL;
  cdcEcmHandle->dataInterfaceHandle = NULL;

  *handle = (class_handle_t)cdcEcmHandle;
  return error;
}

usb_status_t USB_DeviceCdcEcmDeinit(class_handle_t handle) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle;

  cdcEcmHandle = (usb_device_cdc_ecm_struct_t *)handle;
  if (cdcEcmHandle == NULL) {
    return kStatus_USB_InvalidHandle;
  }

  (void)USB_DeviceCdcEcmEndpointsDeinit(cdcEcmHandle,
                                        &cdcEcmHandle->commInterfaceHandle);
  (void)USB_DeviceCdcEcmEndpointsDeinit(cdcEcmHandle,
                                        &cdcEcmHandle->dataInterfaceHandle);
  cdcEcmHandle->handle = NULL;
  cdcEcmHandle->configStruct = NULL;
  cdcEcmHandle->configuration = 0;

  return kStatus_USB_Success;
}

usb_status_t USB_DeviceCdcEcmEvent(void *handle, uint32_t event, void *param) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle;
  usb_device_class_event_t eventCode = (usb_device_class_event_t)event;
  usb_device_cdc_ecm_pipe_t *pipe;
  usb_status_t error = kStatus_USB_Error;
  uint16_t interfaceAlternate;
  uint8_t interfaceNumber;
  uint8_t alternate;
  uint8_t *temp8;

  if (!param || !handle) {
    return kStatus_USB_InvalidHandle;
  }
  cdcEcmHandle = (usb_device_cdc_ecm_struct_t *)handle;
  if (cdcEcmHandle->configStruct == NULL) {
    return error;
  }

  switch (eventCode) {
    case kUSB_DeviceClassEventDeviceReset:
      cdcEcmHandle->configuration = 0;
      cdcEcmHandle->dataAlternate = 0;
      error = kStatus_USB_Success;
      break;
    case kUSB_DeviceClassEventSetConfiguration:
      temp8 = (uint8_t *)param;
      if (*temp8 == cdcEcmHandle->configuration) {
        error = kStatus_USB_Success;
        break;
      }
      (void)USB_DeviceCdcEcmEndpointsDeinit(cdcEcmHandle,
                                            &cdcEcmHandle->commInterfaceHandle);
      cdcEcmHandle->configuration = *temp8;
      error = USB_DeviceCdcEcmEndpointsInit(
          cdcEcmHandle, USB_DEVICE_CDC_ECM_COMM_CLASS_CODE, 0,
          &cdcEcmHandle->commInterfaceHandle,
          &cdcEcmHandle->commInterfaceNumber);
      if (error == kStatus_USB_Success) {
        // Alternate 0 carries no endpoints; the host selects 1 to start.
        error = USB_DeviceCdcEcmDataEndpointsInit(cdcEcmHandle, 0);
      }
      break;
    case kUSB_DeviceClassEventSetInterface:
      interfaceAlternate = *((uint16_t *)param);
      interfaceNumber = (uint8_t)(interfaceAlternate >> 8);
      alternate = (uint8_t)(interfaceAlternate & 0xFF);

      if (interfaceNumber == cdcEcmHandle->commInterfaceNumber) {
        error = (alternate == 0) ? kStatus_USB_Success
                                 : kStatus_USB_InvalidRequest;
        break;
      }
      if (interfaceNumber != cdcEcmHandle->dataInterfaceNumber) {
        break;
      }
      // Re-selecting an alternate also resets the pipes.
      error = USB_DeviceCdcEcmDataEndpointsInit(cdcEcmHandle, alternate);
      break;
    case kUSB_DeviceClassEventSetEndpointHalt:
      temp8 = (uint8_t *)param;
      pipe = USB_DeviceCdcEcmFindPipe(cdcEcmHandle, *temp8);
      if (pipe != NULL) {
        error = USB_DeviceStallEndpoint(cdcEcmHandle->handle, *temp8);
      }
      break;
    case kUSB_DeviceClassEventClearEndpointHalt:
      temp8 = (uint8_t *)param;
      pipe = USB_DeviceCdcEcmFindPipe(cdcEcmHandle, *temp8);
      if (pipe != NULL) {
        // Cancels the pending transfer; its callback reports the loss.
        error = USB_DeviceUnstallEndpoint(cdcEcmHandle->handle, *temp8);
      }
      break;
    case kUSB_DeviceClassEventClassRequest: {
      usb_device_control_request_struct_t *controlRequest =
          (usb_device_control_request_struct_t *)param;
      if ((controlRequest->setup->wIndex & 0xFF) !=
          cdcEcmHandle->commInterfaceNumber) {
        break;
      }
      // Every frame is passed up; the filter value is not needed.
      if (controlRequest->setup->bRequest ==
          USB_DEVICE_CDC_ECM_REQUEST_SET_ETHERNET_PACKET_FILTER) {
        controlRequest->buffer = NULL;
        controlRequest->length = 0;
        error = kStatus_USB_Success;
      } else {
        error = kStatus_USB_InvalidRequest;
      }
      break;
    }
    default:
      break;
  }

  return error;
}

static usb_status_t USB_DeviceCdcEcmTransfer(
    usb_device_cdc_ecm_struct_t *cdcEcmHandle, usb_device_cdc_ecm_pipe_t *pipe,
    uint8_t *buffer, uint32_t length) {
  usb_status_t status;

  if (!cdcEcmHandle) {
    return kStatus_USB_InvalidHandle;
  }
  if (USB_DeviceCdcEcmFindPipe(cdcEcmHandle, pipe->ep) != pipe) {
    return kStatus_USB_Error;
  }
  if (pipe->isBusy) {
    return kStatus_USB_Busy;
  }
  pipe->isBusy = 1;

  if (pipe->ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) {
    status = USB_DeviceSendRequest(cdcEcmHandle->handle, pipe->ep, buffer,
                                   length);
  } else {
    status = USB_DeviceRecvRequest(cdcEcmHandle->handle, pipe->ep, buffer,
                                   length);
  }
  if (status != kStatus_USB_Success) {
    pipe->isBusy = 0;
  }
  return status;
}

usb_status_t USB_DeviceCdcEcmSend(class_handle_t handle, uint8_t *buffer,
                                  uint32_t length) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)handle;
  return USB_DeviceCdcEcmTransfer(cdcEcmHandle, &cdcEcmHandle->bulkIn, buffer,
                                  length);
}

usb_status_t USB_DeviceCdcEcmRecv(class_handle_t handle, uint8_t *buffer,
                                  uint32_t length) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)handle;
  return USB_DeviceCdcEcmTransfer(cdcEcmHandle, &cdcEcmHandle->bulkOut, buffer,
                                  length);
}

usb_status_t USB_DeviceCdcEcmNotify(class_handle_t handle, uint8_t *buffer,
                                    uint32_t length) {
  usb_device_cdc_ecm_struct_t *cdcEcmHandle =
      (usb_device_cdc_ecm_struct_t *)handle;
  return USB_DeviceCdcEcmTransfer(cdcEcmHandle, &cdcEcmHandle->interruptIn,
                                  buffer, length);
}

#endif  // USB_DEVICE_CONFIG_CDC_ECM
