#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#include "usb_host.hpp"
#include <cstring>

static const char *TAG = "USBH";

// Forward-declared in header as a friend
void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    USBhost *host = static_cast<USBhost *>(arg);
    if (!host) {
        ESP_LOGE(TAG, "Callback with null host");
        return;
    }

    ESP_LOGI(TAG, "client event: %d", event_msg ? event_msg->event : -1);

    // If user registered a callback, ALWAYS forward all events.
    if (host->_client_event_cb) {
        host->_client_event_cb(event_msg, arg);
        return;
    }

    // Default behavior only when no user callback is registered
    if (!event_msg) return;

    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "NEW_DEV addr=%d", event_msg->new_dev.address);
            host->open(event_msg);
            break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "DEV_GONE");
            host->close();  // Release resources as per ESP-IDF expectations
            break;

        default:
            // Other events can be safely ignored or logged
            ESP_LOGI(TAG, "Unhandled event: %d", event_msg->event);
            break;
    }
}

static void client_async_seq_task(void *param)
{
    usb_host_client_handle_t client_hdl = *(usb_host_client_handle_t *)param;
    uint32_t event_flags = 0;

    // Keep servicing events indefinitely so reconnects work.
    // The app can later add a stop flag if it needs to terminate this task.
    for (;;) {
        // Service client event queue (dispatches to _client_event_callback)
        esp_err_t cerr = usb_host_client_handle_events(client_hdl, 1);
        if (cerr != ESP_OK && cerr != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "usb_host_client_handle_events: %d", cerr);
        }

        // Service library-level events (port/power/housekeeping)
        esp_err_t lerr = usb_host_lib_handle_events(1, &event_flags);
        if (lerr == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                ESP_LOGI(TAG, "NO_CLIENTS -> freeing all devices");
                usb_host_device_free_all();
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                // In the original file this broke out of the loop.
                // Do NOT break — keep the task alive for future device connections.
                ESP_LOGI(TAG, "ALL_FREE (devices freed). Staying alive for future connections.");
            }
        } else if (lerr != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "usb_host_lib_handle_events: %d", lerr);
        }

        // Be nice to the scheduler
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Unreachable with the current design; kept for completeness.
    usb_host_client_deregister(client_hdl);
    vTaskDelete(NULL);
}

// -------- USBhost methods ----------

USBhost::USBhost()
    : client_hdl(nullptr),
      dev_hdl(nullptr),
      _client_event_cb(nullptr),
      _dev_addr(0),
      _configs(0),
      _itfs(0)
{
    memset(&dev_info, 0, sizeof(dev_info));
}

USBhost::~USBhost()
{
    // Best-effort cleanup
    if (dev_hdl && client_hdl) {
        usb_host_device_close(client_hdl, dev_hdl);
        dev_hdl = nullptr;
    }
    if (client_hdl) {
        usb_host_client_deregister(client_hdl);
        client_hdl = nullptr;
    }
    
    // Free control transfer if allocated
    freeControlTransfer();
    
    // Note: usb_host_uninstall() is not called here since the app may have other clients.
}

bool USBhost::init(bool create_tasks)
{
    const usb_host_config_t config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // INVALID_STATE if already installed
        ESP_LOGE(TAG, "usb_host_install failed: %d", err);
        return false;
    }
    ESP_LOGI(TAG, "usb_host_install: %d", err);

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = _client_event_callback,
            .callback_arg = this
        }
    };

    err = usb_host_client_register(&client_config, &client_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register failed: %d", err);
        return false;
    }
    ESP_LOGI(TAG, "client registered");

    if (create_tasks) {
        BaseType_t ok = xTaskCreate(client_async_seq_task, "usb_async",
                                    4 * 512, &client_hdl, 20, nullptr);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client_async_seq_task");
            // We can still function in synchronous mode, but this class expects async.
            return false;
        }
    }

    return true;
}

bool USBhost::open(const usb_host_client_event_msg_t *event_msg)
{
    if (!client_hdl || !event_msg) {
        ESP_LOGE(TAG, "open: invalid state (client_hdl=%p, event_msg=%p)", client_hdl, event_msg);
        return false;
    }

    // If we somehow still have a device handle, close it first
    if (dev_hdl) {
        ESP_LOGW(TAG, "open: closing previous dev handle before opening new device");
        usb_host_device_close(client_hdl, dev_hdl);
        dev_hdl = nullptr;
    }

    esp_err_t err = usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_device_open failed: %d (addr=%d)", err, event_msg->new_dev.address);
        return false;
    }

    _dev_addr = event_msg->new_dev.address;
    ESP_LOGI(TAG, "Device opened (addr=%u)", _dev_addr);

    // Optional: parse descriptors immediately for convenience
    parseConfig();

    return true;
}

void USBhost::close()
{
    if (!client_hdl) {
        ESP_LOGW(TAG, "close: no client");
        return;
    }
    if (!dev_hdl) {
        ESP_LOGI(TAG, "close: no device to close");
        return;
    }

    // No need to free control transfer - they are allocated per request

    esp_err_t err = usb_host_device_close(client_hdl, dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_device_close: %d", err);
    } else {
        ESP_LOGI(TAG, "Device closed");
    }
    dev_hdl = nullptr;
    _dev_addr = 0;
}

void USBhost::parseConfig()
{
    if (!dev_hdl) {
        ESP_LOGW(TAG, "parseConfig: no device handle");
        return;
    }

    const usb_device_desc_t *device_desc = nullptr;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &device_desc);
    if (err == ESP_OK && device_desc) {
        // ESP_LOG_BUFFER_HEX(TAG, device_desc->val, USB_DEVICE_DESC_SIZE);
        ESP_LOGI(TAG, "Device VID:PID = %04x:%04x",
                 device_desc->idVendor, device_desc->idProduct);
    } else {
        ESP_LOGW(TAG, "get_device_descriptor failed: %d", err);
    }

    const usb_config_desc_t *config_desc = nullptr;
    err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err == ESP_OK && config_desc) {
        // Could parse bNumInterfaces, etc., if useful
        _configs = config_desc->bNumInterfaces ? 1 : 0; // active config present
        _itfs = config_desc->bNumInterfaces;
        ESP_LOGI(TAG, "Active config: itfs=%u", _itfs);
    } else {
        ESP_LOGW(TAG, "get_active_config_descriptor failed: %d", err);
    }
}

usb_device_info_t USBhost::getDeviceInfo()
{
    if (!dev_hdl) {
        ESP_LOGW(TAG, "getDeviceInfo: no device handle");
        memset(&dev_info, 0, sizeof(dev_info));
        return dev_info;
    }

    esp_err_t err = usb_host_device_info(dev_hdl, &dev_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_device_info: %d", err);
        memset(&dev_info, 0, sizeof(dev_info));
    }
    return dev_info;
}

const usb_device_desc_t* USBhost::getDeviceDescriptor()
{
    if (!dev_hdl) return nullptr;
    const usb_device_desc_t *device_desc = nullptr;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &device_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "getDeviceDescriptor failed: %d", err);
        return nullptr;
    }
    return device_desc;
}

const usb_config_desc_t* USBhost::getConfigurationDescriptor()
{
    if (!dev_hdl) return nullptr;
    const usb_config_desc_t *config_desc = nullptr;
    esp_err_t err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "getConfigurationDescriptor failed: %d", err);
        return nullptr;
    }
    return config_desc;
}

uint8_t USBhost::getConfiguration()
{
    return getDeviceInfo().bConfigurationValue;
}

usb_host_client_handle_t USBhost::clientHandle()
{
    return client_hdl;
}

usb_device_handle_t USBhost::deviceHandle()
{
    return dev_hdl;
}

// bool USBhost::setConfiguration(uint8_t); // still not implemented here

// -------- Control Transfer Methods ----------

bool USBhost::allocateControlTransfer(size_t data_buffer_size)
{
    // Deprecated method - transfers are now allocated per request
    ESP_LOGW(TAG, "allocateControlTransfer is deprecated - transfers are allocated per request");
    return true; // Always return true since allocation happens in sendControlTransfer
}

void USBhost::freeControlTransfer()
{
    // Deprecated method - transfers are now freed automatically after completion
    ESP_LOGW(TAG, "freeControlTransfer is deprecated - transfers are freed automatically");
}

esp_err_t USBhost::sendControlTransfer(const usb_setup_packet_t *setup_pkt, 
                                       const void *data, 
                                       size_t data_len,
                                       usb_transfer_cb_t callback,
                                       void *context)
{
    if (!dev_hdl) {
        ESP_LOGE(TAG, "sendControlTransfer: no device handle");
        return ESP_ERR_INVALID_STATE;
    }

    if (!setup_pkt) {
        ESP_LOGE(TAG, "sendControlTransfer: null setup packet");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate transfer for this specific control transfer
    size_t total_buffer_size = sizeof(usb_setup_packet_t) + data_len;
    usb_transfer_t *transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(total_buffer_size, 0, &transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_transfer_alloc failed: %d", err);
        return err;
    }

    // Configure transfer
    transfer->device_handle = dev_hdl;
    transfer->bEndpointAddress = 0; // Control endpoint

    // Copy setup packet to transfer buffer
    memcpy(transfer->data_buffer, setup_pkt, sizeof(usb_setup_packet_t));
    
    // Copy data if provided
    if (data && data_len > 0) {
        memcpy(transfer->data_buffer + sizeof(usb_setup_packet_t), data, data_len);
    }

    // Set transfer parameters
    transfer->num_bytes = sizeof(usb_setup_packet_t) + data_len;
    transfer->callback = callback;
    transfer->context = context;

    // Submit the control transfer
    err = usb_host_transfer_submit_control(client_hdl, transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_transfer_submit_control failed: %d", err);
        usb_host_transfer_free(transfer);
    } else {
        ESP_LOGI(TAG, "Control transfer submitted (request: 0x%02x, length: %zu)", 
                 setup_pkt->bRequest, data_len);
    }

    return err;
}

esp_err_t USBhost::sendControlTransfer(uint8_t bmRequestType, uint8_t bRequest, 
                                       uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                                       const void *data,
                                       usb_transfer_cb_t callback,
                                       void *context)
{
    usb_setup_packet_t setup_pkt;
    setup_pkt.bmRequestType = bmRequestType;
    setup_pkt.bRequest = bRequest;
    setup_pkt.wValue = wValue;
    setup_pkt.wIndex = wIndex;
    setup_pkt.wLength = wLength;

    return sendControlTransfer(&setup_pkt, data, wLength, callback, context);
}

esp_err_t USBhost::getDescriptor(uint8_t desc_type, uint8_t desc_index, 
                                 uint16_t lang_id, uint16_t length,
                                 usb_transfer_cb_t callback, void *context)
{
    uint16_t wValue = (desc_type << 8) | desc_index;
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
        USB_B_REQUEST_GET_DESCRIPTOR,
        wValue,
        lang_id,
        length,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::setAddress(uint8_t address, 
                              usb_transfer_cb_t callback, void *context)
{
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
        USB_B_REQUEST_SET_ADDRESS,
        address,
        0,
        0,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::setConfiguration(uint8_t config_value, 
                                    usb_transfer_cb_t callback, void *context)
{
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
        USB_B_REQUEST_SET_CONFIGURATION,
        config_value,
        0,
        0,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::getConfiguration(usb_transfer_cb_t callback, void *context)
{
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_DEVICE,
        USB_B_REQUEST_GET_CONFIGURATION,
        0,
        0,
        1,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::setInterface(uint8_t interface, uint8_t alt_setting,
                                usb_transfer_cb_t callback, void *context)
{
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
        USB_B_REQUEST_SET_INTERFACE,
        alt_setting,
        interface,
        0,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::getInterface(uint8_t interface,
                                usb_transfer_cb_t callback, void *context)
{
    return sendControlTransfer(
        USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_INTERFACE,
        USB_B_REQUEST_GET_INTERFACE,
        0,
        interface,
        1,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::clearFeature(uint8_t recipient, uint8_t feature, uint16_t index,
                                usb_transfer_cb_t callback, void *context)
{
    uint8_t bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD;
    
    switch (recipient) {
        case USB_BM_REQUEST_TYPE_RECIP_DEVICE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_DEVICE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_INTERFACE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_ENDPOINT:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
            break;
        default:
            ESP_LOGE(TAG, "clearFeature: invalid recipient");
            return ESP_ERR_INVALID_ARG;
    }

    return sendControlTransfer(
        bmRequestType,
        USB_B_REQUEST_CLEAR_FEATURE,
        feature,
        index,
        0,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::setFeature(uint8_t recipient, uint8_t feature, uint16_t index,
                              usb_transfer_cb_t callback, void *context)
{
    uint8_t bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_STANDARD;
    
    switch (recipient) {
        case USB_BM_REQUEST_TYPE_RECIP_DEVICE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_DEVICE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_INTERFACE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_ENDPOINT:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
            break;
        default:
            ESP_LOGE(TAG, "setFeature: invalid recipient");
            return ESP_ERR_INVALID_ARG;
    }

    return sendControlTransfer(
        bmRequestType,
        USB_B_REQUEST_SET_FEATURE,
        feature,
        index,
        0,
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::getStatus(uint8_t recipient, uint16_t index,
                             usb_transfer_cb_t callback, void *context)
{
    uint8_t bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD;
    
    switch (recipient) {
        case USB_BM_REQUEST_TYPE_RECIP_DEVICE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_DEVICE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_INTERFACE:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
            break;
        case USB_BM_REQUEST_TYPE_RECIP_ENDPOINT:
            bmRequestType |= USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
            break;
        default:
            ESP_LOGE(TAG, "getStatus: invalid recipient");
            return ESP_ERR_INVALID_ARG;
    }

    return sendControlTransfer(
        bmRequestType,
        USB_B_REQUEST_GET_STATUS,
        0,
        index,
        2, // Status is 2 bytes
        nullptr,
        callback,
        context
    );
}

esp_err_t USBhost::getDeviceDescriptorAsync(usb_transfer_cb_t callback, void *context)
{
    return getDescriptor(USB_W_VALUE_DT_DEVICE, 0, 0, USB_DEVICE_DESC_SIZE, callback, context);
}

esp_err_t USBhost::getConfigurationDescriptorAsync(uint8_t config_index, uint16_t length,
                                                   usb_transfer_cb_t callback, void *context)
{
    return getDescriptor(USB_W_VALUE_DT_CONFIG, config_index, 0, length, callback, context);
}

esp_err_t USBhost::getStringDescriptorAsync(uint8_t string_index, uint16_t lang_id, uint16_t length,
                                            usb_transfer_cb_t callback, void *context)
{
    return getDescriptor(USB_W_VALUE_DT_STRING, string_index, lang_id, length, callback, context);
}
