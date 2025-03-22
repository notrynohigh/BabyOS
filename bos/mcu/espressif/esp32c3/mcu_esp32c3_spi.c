#include "b_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "hal/inc/b_hal_spi.h"

__WEAKDEF spi_device_handle_t bMcuGetSpiHandle(bHalSPINumber_t spi_num)
{
    return NULL;
}

int bMcuSpiSetSpeed(const bHalSPIIf_t *spi_if, bHalSPISpeed_t speed)
{
    return 0;
}

uint8_t bMcuSpiTransfer(const bHalSPIIf_t *spi_if, uint8_t dat)
{
    if (spi_if == NULL)
    {
        return 0;
    }
    spi_device_handle_t spi_handle = bMcuGetSpiHandle(spi_if->_if.spi);
    if (spi_handle == NULL)
    {
        return -1;
    }
    spi_transaction_t trans = {
        .length    = 8,
        .tx_buffer = &dat,
        .rx_buffer = &dat,
    };
    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    return (ret == ESP_OK) ? dat : 0;
}

int bMcuSpiSend(const bHalSPIIf_t *spi_if, const uint8_t *pbuf, uint16_t len)
{
    if (spi_if == NULL || pbuf == NULL)
    {
        return -1;
    }
    spi_device_handle_t spi_handle = bMcuGetSpiHandle(spi_if->_if.spi);
    if (spi_handle == NULL)
    {
        return -1;
    }
    spi_transaction_t trans = {
        .length    = len * 8,
        .tx_buffer = pbuf,
        .rx_buffer = NULL,
    };
    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    return (ret == ESP_OK) ? 0 : -1;
}

int bMcuSpiReceive(const bHalSPIIf_t *spi_if, uint8_t *pbuf, uint16_t len)
{
    if (spi_if == NULL || pbuf == NULL)
    {
        return -1;
    }
    spi_device_handle_t spi_handle = bMcuGetSpiHandle(spi_if->_if.spi);
    if (spi_handle == NULL)
    {
        return -1;
    }
    spi_transaction_t trans = {
        .length    = len * 8,
        .tx_buffer = NULL,
        .rx_buffer = pbuf,
    };
    esp_err_t ret = spi_device_transmit(spi_handle, &trans);
    return (ret == ESP_OK) ? 0 : -1;
}
