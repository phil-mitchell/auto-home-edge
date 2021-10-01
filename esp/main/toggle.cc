#include "autohome.h"

static const char *TAG = "gpio";

OutputToggle::OutputToggle( gpio_num_t pin, bool status, bool invert )
    : _pin( pin ), _status( status ), _invert( invert )
{
    _config.pin_bit_mask = 0;
    _config.mode = GPIO_MODE_OUTPUT;
}

OutputToggle::~OutputToggle()
{
    off();
}

esp_err_t OutputToggle::init( gpio_num_t pin )
{
    if( pin != (gpio_num_t)-1 && _pin != pin ) {
        if( _config.pin_bit_mask != 0 ) {
            off();
        }
        _pin = pin;
    }

    ESP_LOGI( TAG, "OutputToggle::init %d", _pin );

    _config.pin_bit_mask = BIT( _pin );
    esp_err_t res = gpio_config( &_config );

    if( res != ESP_FAIL ) {
        if( _status ) {
            on();
        } else {
            off();
        }
    }
    return res;
}

void OutputToggle::on()
{
    ESP_ERROR_CHECK( gpio_set_level( _pin, _invert ? 1 : 0 ) );
    _status = true;
}

void OutputToggle::off()
{
    ESP_ERROR_CHECK( gpio_set_level( _pin, _invert ? 0 : 1 ) );
    _status = false;
}

void OutputToggle::toggle()
{
    if( _status ) {
        off();
    } else {
        on();
    }
}

bool OutputToggle::isOn()
{
    return _status;
}

static void flasherTask( void *arg )
{
    Flasher *flasher = (Flasher*)arg;
    uint32_t notifiedValue = ulTaskNotifyTake( pdFALSE, 0 );

    while( !notifiedValue ) {
        flasher->toggle();
        notifiedValue = ulTaskNotifyTake( pdFALSE, flasher->nextDelay() / portTICK_RATE_MS );
    }

    vTaskDelete( NULL );
}


Flasher::Flasher( gpio_num_t pin )
    : OutputToggle( pin ), _onInterval( 0 ), _offInterval( 0 ), _task( NULL )
{
}

Flasher::~Flasher()
{
    setPattern( 0, 0 );
}

void Flasher::setPattern( uint32_t onInterval, uint32_t offInterval )
{
    if( _task != NULL ) {
        xTaskNotifyGive( _task );
        _task = NULL;
    }

    _onInterval = onInterval;
    _offInterval = offInterval;

    if( _onInterval > 0 && _offInterval > 0 ) {
        xTaskCreate( &flasherTask, "flasher", 512, this, 5, &_task );
    } else {
        if( _onInterval > 0 ) {
            on();
        } else {
            off();
        }
    }
}

uint32_t Flasher::nextDelay()
{
    return isOn() ? _onInterval : _offInterval;
}

Switch::Switch( Zone &zone, const char *id )
    : Device( zone, id ), _toggle( (gpio_num_t)-1 )
{
}

Switch::~Switch()
{
}

esp_err_t Switch::init( gpio_num_t pin )
{
    return _toggle.init( pin );
}

void Switch::on()
{
    _toggle.on();
    getZone().setValue( getId(), "switch", true );
}

void Switch::off()
{
    _toggle.off();
    getZone().setValue( getId(), "switch", false );
}
