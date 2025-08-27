# USB Host Control Transfer Example

This example demonstrates how to use the enhanced USB host library with control transfer support. The library now provides comprehensive control transfer functionality for communicating with USB devices.

## Features Added

The USB host library has been expanded with the following control transfer capabilities:

### Basic Control Transfer Management
- `sendControlTransfer()` - Send custom control transfers with setup packets and data
- `allocateControlTransfer()` / `freeControlTransfer()` - **Deprecated**: Transfers are now allocated per request automatically

### Standard USB Control Requests
- `getDescriptor()` - Get any USB descriptor
- `setAddress()` - Set device address
- `setConfiguration()` - Set device configuration
- `getConfiguration()` - Get current configuration
- `setInterface()` - Set interface alternate setting
- `getInterface()` - Get interface alternate setting
- `clearFeature()` - Clear device/interface/endpoint features
- `setFeature()` - Set device/interface/endpoint features
- `getStatus()` - Get device/interface/endpoint status

### Helper Methods
- `getDeviceDescriptorAsync()` - Get device descriptor (async)
- `getConfigurationDescriptorAsync()` - Get configuration descriptor (async)
- `getStringDescriptorAsync()` - Get string descriptor (async)

## Usage

### 1. Initialize USB Host

```cpp
#include "USBhost.h"

USBhost host;

void setup() {
    // Initialize USB host
    if (!host.init()) {
        Serial.println("Failed to initialize USB host");
        return;
    }
    
    // Control transfers are automatically allocated per request
    // No need to pre-allocate buffers
}
```

### 2. Send Control Transfers

#### Using Standard Methods
```cpp
// Get device descriptor
esp_err_t err = host.getDeviceDescriptorAsync(control_transfer_callback);

// Get device status
err = host.getStatus(USB_BM_REQUEST_TYPE_RECIP_DEVICE, 0, control_transfer_callback);

// Set configuration
err = host.setConfiguration(1, control_transfer_callback);
```

#### Using Custom Setup Packets
```cpp
usb_setup_packet_t setup = {
    .bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
    .bRequest = USB_B_REQUEST_GET_STATUS,
    .wValue = 0,
    .wIndex = 0,
    .wLength = 2
};

esp_err_t err = host.sendControlTransfer(&setup, nullptr, 0, control_transfer_callback);
```

#### With Data
```cpp
uint8_t data[] = {0x01, 0x02, 0x03};
esp_err_t err = host.sendControlTransfer(
    USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
    0x01, // Custom request
    0x1234, // wValue
    0x5678, // wIndex
    sizeof(data), // wLength
    data, // Data to send
    control_transfer_callback
);
```

#### HID SET_REPORT Example
```cpp
// Create 256-byte payload
uint8_t hid_payload[256];
memset(hid_payload, 0x00, sizeof(hid_payload));

// Fill with your data (first 48 bytes)
hid_payload[0] = 0x1c; hid_payload[1] = 0x00; // ... fill your data
hid_payload[43] = preset_num; hid_payload[44] = preset_hex;

// Send HID SET_REPORT control transfer
esp_err_t err = host.sendControlTransfer(
    USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
    0x09, // SET_REPORT
    (2 << 8) | 0, // Report type Output=2, Report ID=0
    4, // Interface number
    sizeof(hid_payload),
    hid_payload,
    control_transfer_callback
);
```

### 3. Handle Control Transfer Callbacks

```cpp
void control_transfer_callback(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        Serial.println("Control transfer completed successfully!");
        
        // Access received data
        if (transfer->actual_num_bytes > 0) {
            Serial.print("Received data: ");
            for (int i = 0; i < transfer->actual_num_bytes; i++) {
                Serial.printf("%02X ", transfer->data_buffer[i]);
            }
            Serial.println();
        }
    } else {
        Serial.printf("Control transfer failed with status: %d\n", transfer->status);
    }
}
```

## API Reference

### Control Transfer Management

#### `bool allocateControlTransfer(size_t data_buffer_size = 64)` (Deprecated)
**Deprecated**: This method is kept for backward compatibility but is no longer needed.
- Control transfers are now allocated automatically per request
- **Returns:** Always `true`

#### `void freeControlTransfer()` (Deprecated)
**Deprecated**: This method is kept for backward compatibility but is no longer needed.
- Control transfers are now freed automatically after completion

#### `esp_err_t sendControlTransfer(const usb_setup_packet_t *setup_pkt, const void *data = nullptr, size_t data_len = 0, usb_transfer_cb_t callback = nullptr, void *context = nullptr)`
Sends a control transfer using a setup packet.
- **Parameters:**
  - `setup_pkt`: USB setup packet
  - `data`: Optional data to send
  - `data_len`: Length of data
  - `callback`: Optional callback function
  - `context`: Optional context pointer
- **Returns:** ESP error code

#### `esp_err_t sendControlTransfer(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, const void *data = nullptr, usb_transfer_cb_t callback = nullptr, void *context = nullptr)`
Sends a control transfer using individual parameters.
- **Parameters:**
  - `bmRequestType`: Request type (direction, type, recipient)
  - `bRequest`: Request code
  - `wValue`: Value field
  - `wIndex`: Index field
  - `wLength`: Length field
  - `data`: Optional data to send
  - `callback`: Optional callback function
  - `context`: Optional context pointer
- **Returns:** ESP error code

### Standard USB Control Requests

All standard USB control request methods follow the same pattern:
- Take optional callback and context parameters
- Return ESP error code
- Use the allocated control transfer buffer

#### Descriptor Operations
- `getDescriptor(uint8_t desc_type, uint8_t desc_index, uint16_t lang_id, uint16_t length, ...)`
- `getDeviceDescriptorAsync(...)`
- `getConfigurationDescriptorAsync(uint8_t config_index, uint16_t length, ...)`
- `getStringDescriptorAsync(uint8_t string_index, uint16_t lang_id, uint16_t length, ...)`

#### Device Configuration
- `setAddress(uint8_t address, ...)`
- `setConfiguration(uint8_t config_value, ...)`
- `getConfiguration(...)`

#### Interface Management
- `setInterface(uint8_t interface, uint8_t alt_setting, ...)`
- `getInterface(uint8_t interface, ...)`

#### Feature Management
- `clearFeature(uint8_t recipient, uint8_t feature, uint16_t index, ...)`
- `setFeature(uint8_t recipient, uint8_t feature, uint16_t index, ...)`
- `getStatus(uint8_t recipient, uint16_t index, ...)`

## Error Handling

The library provides comprehensive error handling:
- All methods return ESP error codes
- Callbacks receive transfer status information
- Automatic cleanup on device disconnection
- Buffer size validation

## Notes

- Control transfers are asynchronous by default
- Each control transfer is allocated and freed automatically
- Multiple control transfers can be queued (ESP-IDF handles the queuing)
- All standard USB control requests are supported as per the USB 2.0 specification
- The library follows ESP-IDF USB host API conventions
- **Naming Convention**: Control transfer methods that return `esp_err_t` are suffixed with "Async" to distinguish them from synchronous methods that return descriptor pointers
- **Important**: Callbacks must free the transfer using `usb_host_transfer_free(transfer)` after processing

## Example Output

When running this example with a USB device connected, you should see output similar to:

```
USB Host Control Transfer Example
USB host initialized. Waiting for device...
New device connected at address: 1
Device detected! Allocating control transfer...
Control transfer allocated successfully
Getting device descriptor...
Control transfer completed successfully!
Received data: 12 01 00 02 00 00 00 40 83 04 23 05 00 01 01 02 00 01

--- Running Control Transfer Tests ---
Test 1: Getting device status...
Control transfer completed successfully!
Received data: 00 00
Test 2: Getting current configuration...
Control transfer completed successfully!
Received data: 01
...
```
