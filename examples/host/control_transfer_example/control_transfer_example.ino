#include "USBhost.h"

USBhost host;

// Control transfer callback
void control_transfer_callback(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        Serial.println("Control transfer completed successfully!");
        
        // Print the received data (if any)
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

// Device event callback
void device_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            Serial.printf("New device connected at address: %d\n", event_msg->new_dev.address);
            break;
            
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            Serial.println("Device disconnected");
            break;
            
        default:
            Serial.printf("Unhandled event: %d\n", event_msg->event);
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("USB Host Control Transfer Example");
    
    // Initialize USB host
    if (!host.init()) {
        Serial.println("Failed to initialize USB host");
        return;
    }
    
    // Register device event callback
    host.registerClientCb(device_event_callback);
    
    Serial.println("USB host initialized. Waiting for device...");
}

void loop()
{
    static bool device_ready = false;
    static uint32_t last_test = 0;
    
    // Check if device is connected and ready
    if (host.deviceHandle() && !device_ready) {
        Serial.println("Device detected! Allocating control transfer...");
        
        // Allocate control transfer buffer (needs to be large enough for 256-byte payloads)
        if (host.allocateControlTransfer(256)) {
            device_ready = true;
            Serial.println("Control transfer allocated successfully");
            
            // Get device descriptor
            Serial.println("Getting device descriptor...");
            esp_err_t err = host.getDeviceDescriptorAsync(control_transfer_callback);
            if (err != ESP_OK) {
                Serial.printf("Failed to get device descriptor: %d\n", err);
            }
        } else {
            Serial.println("Failed to allocate control transfer");
        }
    }
    
    // Run periodic control transfer tests
    if (device_ready && (millis() - last_test) > 5000) {
        last_test = millis();
        
        Serial.println("\n--- Running Control Transfer Tests ---");
        
        // Test 1: Get device status
        Serial.println("Test 1: Getting device status...");
        esp_err_t err = host.getStatus(USB_BM_REQUEST_TYPE_RECIP_DEVICE, 0, control_transfer_callback);
        if (err != ESP_OK) {
            Serial.printf("Failed to get device status: %d\n", err);
        }
        
        delay(1000);
        
        // Test 2: Get current configuration
        Serial.println("Test 2: Getting current configuration...");
        err = host.getConfiguration(control_transfer_callback);
        if (err != ESP_OK) {
            Serial.printf("Failed to get configuration: %d\n", err);
        }
        
        delay(1000);
        
        // Test 3: Custom control transfer using helper macro
        Serial.println("Test 3: Custom control transfer using helper macro...");
        usb_setup_packet_t custom_setup;
        USB_SETUP_PACKET_INIT_GET_STATUS(&custom_setup);
        err = host.sendControlTransfer(&custom_setup, nullptr, 0, control_transfer_callback);
        if (err != ESP_OK) {
            Serial.printf("Failed to send custom control transfer: %d\n", err);
        }
        
        delay(1000);
        
        // Test 4: Get string descriptor (if available)
        Serial.println("Test 4: Getting string descriptor...");
        err = host.getStringDescriptorAsync(1, 0x0409, 64, control_transfer_callback); // English language
        if (err != ESP_OK) {
            Serial.printf("Failed to get string descriptor: %d\n", err);
        }
        
        delay(1000);
        
        // Test 5: Vendor-specific control transfer
        Serial.println("Test 5: Vendor-specific control transfer...");
        uint8_t vendor_data[] = {0x01, 0x02, 0x03, 0x04};
        err = host.sendControlTransfer(
            USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
            0x01, // Custom vendor request
            0x1234, // wValue
            0x5678, // wIndex
            sizeof(vendor_data), // wLength
            vendor_data, // Data to send
            control_transfer_callback
        );
        if (err != ESP_OK) {
            Serial.printf("Failed to send vendor control transfer: %d\n", err);
        }
        
        delay(1000);
        
        // Test 6: HID SET_REPORT control transfer with large payload
        Serial.println("Test 6: HID SET_REPORT control transfer...");
        
        // Create 256-byte payload (similar to your Python example)
        uint8_t hid_payload[256];
        memset(hid_payload, 0x00, sizeof(hid_payload)); // Initialize with zeros
        
        // Fill the first 48 bytes with your data
        uint8_t preset_num = 0x01;  // Example preset number
        uint8_t preset_hex = 0x02;  // Example preset hex value
        
        hid_payload[0] = 0x1c; hid_payload[1] = 0x00; hid_payload[2] = 0xb0; hid_payload[3] = 0xa2;
        hid_payload[4] = 0xc7; hid_payload[5] = 0x15; hid_payload[6] = 0x86; hid_payload[7] = 0x9d;
        hid_payload[8] = 0xff; hid_payload[9] = 0xff; hid_payload[10] = 0x00; hid_payload[11] = 0x00;
        hid_payload[12] = 0x00; hid_payload[13] = 0x00; hid_payload[14] = 0x1b; hid_payload[15] = 0x00;
        hid_payload[16] = 0x00; hid_payload[17] = 0x02; hid_payload[18] = 0x00; hid_payload[19] = 0x07;
        hid_payload[20] = 0x00; hid_payload[21] = 0x00; hid_payload[22] = 0x02; hid_payload[23] = 0x08;
        hid_payload[24] = 0x01; hid_payload[25] = 0x00; hid_payload[26] = 0x00; hid_payload[27] = 0x00;
        hid_payload[28] = 0x21; hid_payload[29] = 0x09; hid_payload[30] = 0x00; hid_payload[31] = 0x02;
        hid_payload[32] = 0x04; hid_payload[33] = 0x00; hid_payload[34] = 0x00; hid_payload[35] = 0x01;
        hid_payload[36] = 0xe0; hid_payload[37] = 0xa2; hid_payload[38] = 0x05; hid_payload[39] = 0x00;
        hid_payload[40] = 0xb7; hid_payload[41] = 0x00; hid_payload[42] = 0x06; hid_payload[43] = preset_num;
        hid_payload[44] = preset_hex; hid_payload[45] = 0x00; hid_payload[46] = 0x00; hid_payload[47] = 0x00;
        // Remaining bytes are already zero from memset
        
        // Send HID SET_REPORT control transfer (equivalent to your Python code)
        // bmRequestType = 0x21 (Host to device | Class | Interface)
        // bRequest = 0x09 (SET_REPORT)
        // wValue = (2 << 8) | 0 (Report type Output=2, Report ID=0)
        // wIndex = 4 (Interface number)
        err = host.sendControlTransfer(
            USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
            0x09, // SET_REPORT
            (2 << 8) | 0, // Report type Output=2, Report ID=0
            4, // Interface number
            sizeof(hid_payload), // 256 bytes
            hid_payload,
            control_transfer_callback
        );
        if (err != ESP_OK) {
            Serial.printf("Failed to send HID SET_REPORT: %d\n", err);
        } else {
            Serial.println("HID SET_REPORT sent successfully!");
        }
        
        Serial.println("--- Tests completed ---\n");
    }
    
    delay(100);
}
