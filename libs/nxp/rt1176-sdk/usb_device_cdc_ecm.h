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

#ifndef _USB_DEVICE_CDC_ECM_H_
#define _USB_DEVICE_CDC_ECM_H_

// NXP's usb_device_class_type_t has no ECM entry; first value past its end.
#define kUSB_DeviceClassTypeEcm \
  ((usb_device_class_type_t)(kUSB_DeviceClassTypeCcid + 1))

#define USB_DEVICE_CDC_ECM_COMM_CLASS_CODE (0x02)
#define USB_DEVICE_CDC_ECM_COMM_SUBCLASS_CODE (0x06)
#define USB_DEVICE_CDC_ECM_DATA_CLASS_CODE (0x0A)

#define USB_DEVICE_CDC_ECM_REQUEST_SET_ETHERNET_PACKET_FILTER (0x43)

#define USB_DEVICE_CDC_ECM_NOTIF_NETWORK_CONNECTION (0x00)
#define USB_DEVICE_CDC_ECM_NOTIF_CONNECTION_SPEED_CHANGE (0x2A)

typedef enum _usb_device_cdc_ecm_event {
  kUSB_DeviceEcmEventRecvResponse = 0x1,
  kUSB_DeviceEcmEventSendResponse,
  kUSB_DeviceEcmEventNotifyResponse,
} usb_device_cdc_ecm_event_t;

typedef struct _usb_device_cdc_ecm_pipe {
  uint8_t ep;
  uint8_t isBusy;
} usb_device_cdc_ecm_pipe_t;

typedef struct _usb_device_cdc_ecm_struct {
  usb_device_handle handle;
  usb_device_class_config_struct_t *configStruct;
  usb_device_interface_struct_t *commInterfaceHandle;
  usb_device_interface_struct_t *dataInterfaceHandle;
  usb_device_cdc_ecm_pipe_t interruptIn;
  usb_device_cdc_ecm_pipe_t bulkIn;
  usb_device_cdc_ecm_pipe_t bulkOut;
  uint8_t configuration;
  uint8_t commInterfaceNumber;
  uint8_t dataInterfaceNumber;
  uint8_t dataAlternate;
} usb_device_cdc_ecm_struct_t;

#if defined(__cplusplus)
extern "C" {
#endif

usb_status_t USB_DeviceCdcEcmInit(uint8_t controllerId,
                                  usb_device_class_config_struct_t *config,
                                  class_handle_t *handle);

usb_status_t USB_DeviceCdcEcmDeinit(class_handle_t handle);

usb_status_t USB_DeviceCdcEcmEvent(void *handle, uint32_t event, void *param);

// Bulk IN. One Ethernet frame per call; the endpoint adds the ZLP.
usb_status_t USB_DeviceCdcEcmSend(class_handle_t handle, uint8_t *buffer,
                                  uint32_t length);

// Bulk OUT. Completes on a short packet, so |length| must exceed one frame.
usb_status_t USB_DeviceCdcEcmRecv(class_handle_t handle, uint8_t *buffer,
                                  uint32_t length);

// Interrupt IN.
usb_status_t USB_DeviceCdcEcmNotify(class_handle_t handle, uint8_t *buffer,
                                    uint32_t length);

#if defined(__cplusplus)
}
#endif

#endif  // _USB_DEVICE_CDC_ECM_H_
