#pragma once
#include "usb/usb_host.h"

class USBhost
{
    friend void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg);

protected:
    usb_device_info_t dev_info;
    usb_host_client_handle_t client_hdl;
    usb_device_handle_t dev_hdl;
    
    usb_host_client_event_cb_t _client_event_cb = nullptr;
    uint8_t _dev_addr;
    uint8_t _configs;
    uint8_t _itfs;

    // Control transfer management (no longer using persistent allocation)

public:
    USBhost();
    ~USBhost();


    bool init(bool create_tasks = true);
    bool open(const usb_host_client_event_msg_t *event_msg);
    void close();
    usb_device_info_t getDeviceInfo();
    const usb_device_desc_t* getDeviceDescriptor();
    const usb_config_desc_t* getConfigurationDescriptor();

    uint8_t getConfiguration();
    bool setConfiguration(uint8_t);
    void parseConfig();

    usb_host_client_handle_t clientHandle();
    usb_device_handle_t deviceHandle();
    
    void registerClientCb(usb_host_client_event_cb_t cb) { _client_event_cb = cb; }

    // Control transfer methods (deprecated - transfers are now allocated per request)
    // These methods are kept for backward compatibility but are no longer needed
    bool allocateControlTransfer(size_t data_buffer_size = 64);
    void freeControlTransfer();
    
    // Basic control transfer methods
    esp_err_t sendControlTransfer(const usb_setup_packet_t *setup_pkt, 
                                 const void *data = nullptr, 
                                 size_t data_len = 0,
                                 usb_transfer_cb_t callback = nullptr,
                                 void *context = nullptr);
    
    esp_err_t sendControlTransfer(uint8_t bmRequestType, uint8_t bRequest, 
                                 uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                                 const void *data = nullptr,
                                 usb_transfer_cb_t callback = nullptr,
                                 void *context = nullptr);
    
    // Standard USB control requests
    esp_err_t getDescriptor(uint8_t desc_type, uint8_t desc_index, 
                           uint16_t lang_id, uint16_t length,
                           usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t setAddress(uint8_t address, 
                        usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t setConfiguration(uint8_t config_value, 
                              usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t getConfiguration(usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t setInterface(uint8_t interface, uint8_t alt_setting,
                          usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t getInterface(uint8_t interface,
                          usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t clearFeature(uint8_t recipient, uint8_t feature, uint16_t index,
                          usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t setFeature(uint8_t recipient, uint8_t feature, uint16_t index,
                        usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    
    esp_err_t getStatus(uint8_t recipient, uint16_t index,
                       usb_transfer_cb_t callback = nullptr, void *context = nullptr);

    // Helper methods for common control transfers
    esp_err_t getDeviceDescriptorAsync(usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    esp_err_t getConfigurationDescriptorAsync(uint8_t config_index, uint16_t length,
                                             usb_transfer_cb_t callback = nullptr, void *context = nullptr);
    esp_err_t getStringDescriptorAsync(uint8_t string_index, uint16_t lang_id, uint16_t length,
                                      usb_transfer_cb_t callback = nullptr, void *context = nullptr);

};
