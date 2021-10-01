#include "autohome.h"

static const char *TAG = "sensor";
static const float DEFAULT_TEMPERATURE_THRESHOLD = 0.2;

static void dsTask( void *arg )
{
    DS18X20Sensor *sensor = (DS18X20Sensor*)arg;
    uint32_t notifiedValue = ulTaskNotifyTake( pdFALSE, 0 );

    while( !notifiedValue ) {
        sensor->read();
        notifiedValue = ulTaskNotifyTake( pdFALSE, sensor->getInterval() / portTICK_RATE_MS );
    }

    ESP_LOGI( TAG, "DS18X20 task finished\n" );
    vTaskDelete( NULL );
}

DS18X20Sensor::DS18X20Sensor( Zone &zone, const char *id )
    : Device( zone, id ), _pin( (gpio_num_t)-1 ), _addr( ds18x20_ANY ), _interval( 0 ), _task( NULL )
{
}

DS18X20Sensor::~DS18X20Sensor()
{
    setInterval( 0 );
}
 
esp_err_t DS18X20Sensor::init( gpio_num_t pin, ds18x20_addr_t addr )
{
    _pin = pin;
    _addr = addr;
    ESP_LOGI( TAG, "DS18X20Sensor::init %d: %llx", _pin, _addr );

    if( !( BIT( _pin ) & VALID_DEVICE_PIN_MASK ) ) {
        ESP_LOGE( TAG, "Pin not available for device communication" );
        return ESP_FAIL;
    }

    if( _addr == ds18x20_ANY ) {
        ds18x20_addr_t addrs[8];
        int count = ds18x20_scan_devices( _pin, addrs, 8 );
        if( count < 1 ) {
            ESP_LOGE( TAG, "Could not find any DS18X20 sensor on pin %d", _pin );
            return ESP_FAIL;
        } else if( count > 1 ) {
            ESP_LOGE( TAG, "Found multiple DS18X20 sensors on pin %d; specify a sensor address", _pin );
            for( int i = 0; i < count && i < 8; i++ ) {
                ESP_LOGE( TAG, " - %llx", addrs[i] );
            }
            return ESP_FAIL;
        }
        _addr = addrs[0];
    }

    return ESP_OK;
}

void DS18X20Sensor::setInterval( uint32_t interval )
{
    ESP_LOGI( TAG, "DS18X20Sensor::setInterval %d (existing task %p)", interval, _task );

    if( _task != NULL ) {
        ESP_LOGI( TAG, "Sending notification" );
        xTaskNotifyGive( _task );
        ESP_LOGI( TAG, "Sent notification" );
        _task = NULL;
    }

    _interval = interval;
    if( _interval > 0 ) {
        xTaskCreate( &dsTask, "dsmonitor", 4096, this, 5, &_task );
        ESP_LOGI( TAG, "DS18X20Sensor::setInterval %d (created task %p)", interval, _task );
    }
}

uint32_t DS18X20Sensor::getInterval() const
{
    return _interval;
}

void DS18X20Sensor::read()
{
    float temperature;
    float threshold = DEFAULT_TEMPERATURE_THRESHOLD;
    Zone &zone = getZone();

    esp_err_t err = ds18x20_measure_and_read( _pin, _addr, &temperature );
    if( !err ) {
        const DeviceCalibration *calibration = findCalibration( "temperature" );
        if( calibration ) {
            calibration->adjust( temperature );
            threshold = calibration->doubleThreshold();
        }
        
        zone.setValue( getId(), "temperature", temperature, "celsius", threshold );
    }
}
