#include "autohome.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctime>
#include <algorithm>

static const char *TAG = "zone";

Zone::Zone( MQTTClient &client, const char *homeId, const char *zoneId )
    : _client( client )
{
    if( homeId ) {
        strncpy( _homeId, homeId, sizeof( _homeId ) - 1 );
        _homeId[sizeof( _homeId ) - 1] = '\0';
    } else {
        _homeId[0] = '\0';
    }

    if( zoneId ) {
        strncpy( _zoneId, zoneId, sizeof( _zoneId ) - 1 );
        _zoneId[sizeof( _zoneId ) - 1] = '\0';
    } else {
        _zoneId[0] = '\0';
    }
    
    ESP_LOGI( TAG, "Created zone with home %s and zone %s", _homeId, _zoneId );
}

Zone::~Zone()
{
    clearDevices();
}

void Zone::addDevice( Device *device )
{
    if( findDevice( device->getId() ) == NULL ) {
        _devices.push_front( device );
    }
}

void Zone::removeDevice( const char *deviceId )
{
    DeviceList::iterator it = std::find_if(
        _devices.begin(), _devices.end(),
        [deviceId](const Device *device) {
            return strcmp( device->getId(), deviceId ) == 0;
        });

    if( it != _devices.end() ) {
        delete *it;
        _devices.erase( it );
    }
}

Device *Zone::findDevice( const char *deviceId )
{
    DeviceList::iterator it = std::find_if(
        _devices.begin(), _devices.end(),
        [deviceId](const Device *device) {
            return strcmp( device->getId(), deviceId ) == 0;
        });

    if( it != _devices.end() ) {
        return *it;
    }

    return NULL;
}

void Zone::clearDevices()
{
    for( DeviceList::iterator device = _devices.begin(); device != _devices.end(); ++device ) {
        delete *device;
    }
    _devices.clear();
}

bool compareSchedule( const Schedule &first, const Schedule &second )
{
    return first.getHour() < second.getHour() || (
        first.getHour() == second.getHour() && first.getMinute() < second.getMinute() );
}

void Zone::addSchedule( cJSON *json )
{
    _schedules.emplace_front( json, _homeId, _zoneId );
    _schedules.sort( compareSchedule );
}

void Zone::clearSchedules()
{
    _schedules.clear();
}

bool compareOverride( const Override &first, const Override &second )
{
    return first.getStart() < second.getStart() || (
        first.getStart() == second.getStart() && first.getEnd() < second.getEnd() );
}

void Zone::addOverride( cJSON *json )
{
    _overrides.emplace_front( json, _homeId, _zoneId );
    _overrides.sort( compareOverride );
}

void Zone::clearOverrides()
{
    _overrides.clear();
}

Device* Zone::getDevice( const char *deviceId )
{
    DeviceList::iterator it = std::find_if(
        _devices.begin(), _devices.end(),
        [deviceId](const Device *device) {
            return strcmp( device->getId(), deviceId ) == 0;
        });

    if( it == _devices.end() ) {
        return *it;
    }
    return NULL;
}

bool Zone::matches( const char *home, const char *zone ) const
{
    return( strcmp( home, _homeId ) == 0 && strcmp( zone, _zoneId ) == 0 );
}

bool Zone::matchesZone( const char **path, size_t pathlen )
{
    if( pathlen < 4 ) {
        return false;
    }

    return( strcmp( path[0], "homes" ) == 0 && strcmp( path[1], _homeId ) == 0 &&
            strcmp( path[2], "zones" ) == 0 && strcmp( path[3], _zoneId ) == 0 );
}

void Zone::configureZoneDeviceJSON( const char *deviceId, cJSON *json )
{
    cJSON *interface = cJSON_GetObjectItemCaseSensitive( json, "interface" );
    if( !interface ) {
        removeDevice( deviceId );
        return;
    }

    cJSON *interfaceType = cJSON_GetObjectItemCaseSensitive( interface, "type" );
    if( !interfaceType ) {
        removeDevice( deviceId );
        return;
    }

    cJSON *interfaceAddress = cJSON_GetObjectItemCaseSensitive( interface, "address" );
    if( !interfaceAddress ) {
        removeDevice( deviceId );
        return;
    }

    cJSON *interval = cJSON_GetObjectItemCaseSensitive( interface, "interval" );

    Device *device = findDevice( deviceId );
    if( device && !device->is( interfaceType->valuestring ) ) {
        removeDevice( deviceId );
        device = NULL;
    }
    
    if( strcmp( interfaceType->valuestring, "dht11" ) == 0 ) {
        if( !device ) {
            ESP_LOGI( TAG, "Creating new DHT11 sensor" );
            device = new DHTSensor( *this, deviceId );
        }
        ESP_LOGI( TAG, "Initializing DHT11 sensor" );
        esp_err_t res = ((DHTSensor*)device)->init( (gpio_num_t)atoi( interfaceAddress->valuestring ), DHT_TYPE_DHT11, false );
        if( res != ESP_OK ) {
            ESP_LOGI( TAG, "Failed to initialize device %s: %d", deviceId, res );
            delete device;
            device = NULL;
        }
    } else if( strcmp( interfaceType->valuestring, "dht22" ) == 0 ) {
        if( !device ) {
            ESP_LOGI( TAG, "Creating new DHT22 sensor" );
            device = new DHTSensor( *this, deviceId );
        }
        ESP_LOGI( TAG, "Initializing DHT22 sensor" );
        esp_err_t res = ((DHTSensor*)device)->init( (gpio_num_t)atoi( interfaceAddress->valuestring ), DHT_TYPE_AM2301, false );
        if( res != ESP_OK ) {
            ESP_LOGI( TAG, "Failed to initialize device %s: %d", deviceId, res );
            delete device;
            device = NULL;
        }
    } else if( strcmp( interfaceType->valuestring, "ds18x20" ) == 0 ) {
        char *address = strdup( interfaceAddress->valuestring );

        char *part = strsep( &address, ":" );

        gpio_num_t pin = (gpio_num_t)atoi( part );
        ds18x20_addr_t dsAddr = ds18x20_ANY;

        part = strsep( &address, ":" );
        if( part ) {
            dsAddr = strtoull( part, NULL, 16 );
        }

        if( !device ) {
            ESP_LOGI( TAG, "Creating new DS18x20 sensor" );
            device = new DS18X20Sensor( *this, deviceId );
        }

        ESP_LOGI( TAG, "Initializing DS18x20 sensor" );
        esp_err_t res = ((DS18X20Sensor*)device)->init( pin, dsAddr );
        if( res != ESP_OK ) {
            ESP_LOGI( TAG, "Failed to initialize device %s: %d", deviceId, res );
            delete device;
            device = NULL;
        }

        free( address );
    } else if( strcmp( interfaceType->valuestring, "gpio" ) == 0 ) {
        if( !device ) {
            ESP_LOGI( TAG, "Creating new switch" );
            device = new Switch( *this, deviceId );
        }
        ESP_LOGI( TAG, "Initializing switch" );
        esp_err_t res = ((Switch*)device)->init( (gpio_num_t)atoi( interfaceAddress->valuestring ) );
        if( res != ESP_OK ) {
            ESP_LOGI( TAG, "Failed to initialize device %s: %d", deviceId, res );
            delete device;
            device = NULL;
        }
    } else {
        ESP_LOGI( TAG, "Unknown device type %s", interfaceType->valuestring );
    }

    if( device != NULL ) {
        cJSON *changes = cJSON_GetObjectItemCaseSensitive( json, "changes" );
        if( changes != NULL && cJSON_IsArray( changes ) ) {
            device->clearChanges();
            ESP_LOGI( TAG, "Adding changes" );
            int numChanges = cJSON_GetArraySize( changes );
            for( int i = 0; i < numChanges; ++i ) {
                ESP_LOGI( TAG, "Adding change %d of %d", i, numChanges );
                device->addChange( cJSON_GetArrayItem( changes, i ) );
            }
        }
    }

    if( device != NULL ) {
        cJSON *calibrations = cJSON_GetObjectItemCaseSensitive( json, "calibrations" );
        if( calibrations != NULL && cJSON_IsArray( calibrations ) ) {
            device->clearCalibrations();
            ESP_LOGI( TAG, "Adding calibrations" );
            int numCalibrations = cJSON_GetArraySize( calibrations );
            for( int i = 0; i < numCalibrations; ++i ) {
                ESP_LOGI( TAG, "Adding calibration %d of %d", i, numCalibrations );
                device->addCalibration( cJSON_GetArrayItem( calibrations, i ) );
            }
        }
    }

    if( device != NULL ) {
        if( interval != NULL && cJSON_IsNumber( interval ) ) {
            ESP_LOGI( TAG, "Setting update interval for device %s to %d", deviceId, interval->valueint );
            device->setInterval( interval->valueint );
        } else {
            ESP_LOGI( TAG, "Setting update interval for device %s to default", deviceId );
            device->setInterval( 60000 );
        }
    }

    if( device != NULL ) {
        addDevice( device );
    }
}

void Zone::configureZoneJSON( const char **path, size_t pathlen, cJSON *json )
{
    bool local = matchesZone( path, pathlen );

    if( pathlen == 7 && strcmp( path[4], "devices" ) == 0 && path[5] != NULL && path[6] != NULL ) {
        const char *deviceId = path[5];
        const char *type = path[6];
        bool isConfig = strcmp( type, "config" ) == 0;

        if( local && isConfig ) {
            ESP_LOGI( TAG, "Configuring device with id %s", deviceId );
            configureZoneDeviceJSON( deviceId, json );
        } else if( !local && !isConfig ) {
            setRemoteValueJSON( path[1], path[3], deviceId, type, json );
        }
    } else if( local && pathlen == 5 && strcmp( path[4], "config" ) == 0 ) {
        ESP_LOGI( TAG, "Configuring zone details for %s", _zoneId );
        cJSON *schedules = cJSON_GetObjectItem( json, "schedules" );
        if( schedules && cJSON_IsArray( schedules ) ) {
            clearSchedules();
            int numSchedules = cJSON_GetArraySize( schedules );
            for( int i = 0; i < numSchedules; ++i ) {
                addSchedule( cJSON_GetArrayItem( schedules, i ) );
            }
        }

        cJSON *overrides = cJSON_GetObjectItem( json, "overrides" );
        if( overrides && cJSON_IsArray( overrides ) ) {
            clearOverrides();
            int numOverrides = cJSON_GetArraySize( overrides );
            for( int i = 0; i < numOverrides; ++i ) {
                addOverride( cJSON_GetArrayItem( overrides, i ) );
            }
        }
    }
}

void Zone::setRemoteValueJSON( const char *homeId, const char *zoneId, const char *deviceId, const char *type, cJSON *json )
{
    ESP_LOGI( TAG, "Processing remote %s value for home %s zone %s device %s", type, homeId, zoneId, deviceId );
}

void Zone::sendDeviceReadingJSON( const char *deviceId, const char *type, cJSON *value, cJSON *target, cJSON *threshold )
{
    time_t now;
    char buf[ sizeof( "2011-10-08T07:07:09Z" ) ];

    time( &now );
    strftime( buf, sizeof( buf ), "%FT%TZ", gmtime( &now ) );
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject( root, "time", buf );

    if( value ) {
        cJSON_AddItemToObject( root, "value", value );
    }

    if( target ) {
        cJSON_AddItemToObject( root, "target", target );
    }

    if( threshold ) {
        cJSON_AddItemToObject( root, "threshold", threshold );
    }

    char *message = cJSON_PrintUnformatted( root );
    cJSON_Delete( root );

    char *topic = (char*)malloc( 256 );
    if( topic != NULL ) {
        snprintf( topic, 255, "homes/%s/zones/%s/devices/%s/%s", _homeId, _zoneId, deviceId, type );
        topic[255] = '\0';

        _client.publish( topic, message );
        free( topic );
    }
    
    free( message );
}

const DeviceTarget* Zone::findDeviceTarget( const char *deviceId, const char *type ) const
{
    time_t now;
    time( &now );

    ESP_LOGI( TAG, "Looking for override for time %ld", now );
    
    const DeviceTarget *target = NULL;

    for( OverrideList::const_iterator s = _overrides.cbegin(); s != _overrides.cend(); ++s ) {
        ESP_LOGI( TAG, "Checking override for %ld -> %ld", s->getStart(), s->getEnd() );
        if( s->getStart() <= now && s->getEnd() > now ) {
            ESP_LOGI( TAG, "Checking if override matches device" );
            const DeviceTarget *maybeTarget = s->getTarget( _homeId, _zoneId, deviceId, type );
            if( maybeTarget ) {
                ESP_LOGI( TAG, "Override matches device" );
                target = maybeTarget;
            }
        }
    }

    if( !target ) {
        struct tm *tmnow = localtime( &now );
        ESP_LOGI( TAG, "Looking for schedule for day %d hour %d minute %d", tmnow->tm_wday, tmnow->tm_hour, tmnow->tm_min );
        for( ScheduleList::const_iterator s = _schedules.cbegin(); s != _schedules.cend(); ++s ) {
            if( s->getDays() & BIT( tmnow->tm_wday ) &&
                ( s->getHour() < tmnow->tm_hour || (
                    s->getHour() == tmnow->tm_hour  &&
                    s->getMinute() <= tmnow->tm_min ) ) ) {
                const DeviceTarget *maybeTarget = s->getTarget( _homeId, _zoneId, deviceId, type );
                if( maybeTarget ) {
                    target = maybeTarget;
                }
            }
        }
    }

    return target;
}

void Zone::setValue( const char *deviceId, const char *type, double value, const char *unit, double threshold )
{
    cJSON *valueJSON = cJSON_CreateObject();
    cJSON *thresholdJSON = cJSON_CreateObject();
    cJSON *targetJSON = NULL;

    const DeviceTarget *target = findDeviceTarget( deviceId, type );
    if( target ) {
        targetJSON = cJSON_CreateObject();
        cJSON_AddNumberToObject( targetJSON, "value", target->doubleValue() );
        cJSON_AddStringToObject( targetJSON, "unit", target->unit() );
    }

    cJSON_AddNumberToObject( valueJSON, "value", value );
    cJSON_AddStringToObject( valueJSON, "unit", unit );

    cJSON_AddNumberToObject( thresholdJSON, "value", threshold );
    cJSON_AddStringToObject( thresholdJSON, "unit", unit );

    sendDeviceReadingJSON( deviceId, type, valueJSON, targetJSON, thresholdJSON );

    if( target ) {
        takeAction( _homeId, _zoneId, deviceId, type, value, unit, target->doubleValue(), target->unit(), threshold );
    }
}

void Zone::setValue( const char *deviceId, const char *type, int value, const char *unit, int threshold )
{
    cJSON *valueJSON = cJSON_CreateObject();
    cJSON *targetJSON = NULL;
    cJSON *thresholdJSON = cJSON_CreateObject();

    const DeviceTarget *target = findDeviceTarget( deviceId, type );
    if( target ) {
        targetJSON = cJSON_CreateObject();
        cJSON_AddNumberToObject( targetJSON, "value", target->intValue() );
        cJSON_AddStringToObject( targetJSON, "unit", target->unit() );
    }

    cJSON_AddNumberToObject( valueJSON, "value", value );
    cJSON_AddStringToObject( valueJSON, "unit", unit );

    cJSON_AddNumberToObject( thresholdJSON, "value", threshold );
    cJSON_AddStringToObject( thresholdJSON, "unit", unit );

    sendDeviceReadingJSON( deviceId, type, valueJSON, targetJSON, thresholdJSON );

    if( target ) {
        takeAction( _homeId, _zoneId, deviceId, type, value, unit, target->intValue(), target->unit(), threshold );
    }
}

void Zone::setValue( const char *deviceId, const char *type, bool value )
{
    cJSON *valueJSON = cJSON_CreateObject();
    cJSON *targetJSON = NULL;

    const DeviceTarget *target = findDeviceTarget( deviceId, type );
    if( target ) {
        targetJSON = cJSON_CreateObject();
        cJSON_AddNumberToObject( targetJSON, "value", target->boolValue() );
        cJSON_AddStringToObject( targetJSON, "unit", target->unit() );
    }

    cJSON_AddNumberToObject( valueJSON, "value", value ? 1 : 0 );
    cJSON_AddStringToObject( valueJSON, "unit", "" );

    sendDeviceReadingJSON( deviceId, type, valueJSON, targetJSON );

    if( target ) {
        takeAction( _homeId, _zoneId, deviceId, type, value, target->boolValue() );
    }
}

void Zone::takeAction( const char *homeId, const char *zoneId, const char *deviceId, const char *type, double value, const char *unit, double targetValue, const char *targetUnit, double threshold )
{
    ESP_LOGI( TAG, "Taking action for %s value for home %s zone %s device %s", type, homeId, zoneId, deviceId );
    if( value >= ( targetValue - threshold ) && value <= ( targetValue + threshold ) ) {
        ESP_LOGI( TAG, "%s value for home %s zone %s device %s is within threshold", type, homeId, zoneId, deviceId );
        return;
    }

    for( DeviceList::iterator device = _devices.begin(); device != _devices.end(); ++device ) {
        ESP_LOGI( TAG, "Checking device %s", (*device)->getId() );
        const DeviceChangeList &changes = (*device)->getChanges();
        for( DeviceChangeList::const_iterator change = changes.cbegin(); change != changes.cend(); ++change ) {
            if( change->matches( homeId, zoneId, deviceId, type ) ) {
                if( ( change->getDirection() > 0 ) == ( value < targetValue ) ) {
                    ESP_LOGI( TAG, "device %s turning ON", (*device)->getId() );
                    (*device)->on();
                } else {
                    ESP_LOGI( TAG, "device %s turning OFF", (*device)->getId() );
                    (*device)->off();
                }
            }
        }
    }
}

void Zone::takeAction( const char *homeId, const char *zoneId, const char *deviceId, const char *type, int value, const char *unit, int targetValue, const char *targetUnit, int threshold )
{
    ESP_LOGI( TAG, "Taking action for %s value for home %s zone %s device %s", type, homeId, zoneId, deviceId );
    if( value >= ( targetValue - threshold ) && value <= ( targetValue + threshold ) ) {
        ESP_LOGI( TAG, "%s value for home %s zone %s device %s is within threshold", type, homeId, zoneId, deviceId );
        return;
    }

    for( DeviceList::iterator device = _devices.begin(); device != _devices.end(); ++device ) {
        const DeviceChangeList &changes = (*device)->getChanges();
        for( DeviceChangeList::const_iterator change = changes.cbegin(); change != changes.cend(); ++change ) {
            if( change->matches( homeId, zoneId, deviceId, type ) ) {
                if( ( change->getDirection() > 0 ) == ( value < targetValue ) ) {
                    ESP_LOGI( TAG, "device %s turning ON", (*device)->getId() );
                    (*device)->on();
                } else {
                    ESP_LOGI( TAG, "device %s turning OFF", (*device)->getId() );
                    (*device)->off();
                }
            }
        }
    }
}

void Zone::takeAction( const char *homeId, const char *zoneId, const char *deviceId, const char *type, bool value, bool targetValue )
{
    ESP_LOGI( TAG, "Taking action for %s value for home %s zone %s device %s", type, homeId, zoneId, deviceId );
    if( value == targetValue ) {
        ESP_LOGI( TAG, "%s value for home %s zone %s device %s matches target", type, homeId, zoneId, deviceId );
        return;
    }

    for( DeviceList::iterator device = _devices.begin(); device != _devices.end(); ++device ) {
        const DeviceChangeList &changes = (*device)->getChanges();
        for( DeviceChangeList::const_iterator change = changes.cbegin(); change != changes.cend(); ++change ) {
            if( change->matches( homeId, zoneId, deviceId, type ) ) {
                if( ( change->getDirection() > 0 ) == ( value < targetValue ) ) {
                    ESP_LOGI( TAG, "device %s turning ON", (*device)->getId() );
                    (*device)->on();
                } else {
                    ESP_LOGI( TAG, "device %s turning OFF", (*device)->getId() );
                    (*device)->off();
                }
            }
        }
    }
}

Schedule::Schedule( cJSON *json, const char *defaultHomeId, const char *defaultZoneId )
    : _days( 0 ), _hour( 0 ), _minute( 0 )
{
    {
        cJSON *days = cJSON_GetObjectItem( json, "days" );
        if( days && cJSON_IsArray( days ) ) {
            int numDays = cJSON_GetArraySize( days );
            for( int i = 0; i < numDays; ++i ) {
                cJSON *day = cJSON_GetArrayItem( days, i );
                _days |= BIT( day->valueint );
            }
        }
    }

    {
        cJSON *start = cJSON_GetObjectItem( json, "start" );
        if( start && cJSON_IsString( start ) ) {
            char *startstr = strdup( start->valuestring );
            char *origstartstr = startstr;
            char *part = strsep( &startstr, ":" );
            if( part ) {
                _hour = (uint8_t)atoi( part );
                part = strsep( &startstr, ":" );
            }
            if( part ) {
                _minute = (uint8_t)atoi( part );
            }
            free( origstartstr );
        }
    }

    {
        cJSON *changes = cJSON_GetObjectItem( json, "changes" );
        if( changes && cJSON_IsArray( changes ) ) {
            int numChanges = cJSON_GetArraySize( changes );
            for( int i = 0; i < numChanges; ++i ) {
                _targets.emplace_front( cJSON_GetArrayItem( changes, i ), defaultHomeId, defaultZoneId );
            }
        }
    }

    ESP_LOGI( TAG, "Created schedule for days %x starting at %02d:%02d", _days, _hour, _minute ); 
}

Schedule::~Schedule()
{
}

const DeviceTarget* Schedule::getTarget( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const
{
    DeviceTargetList::const_iterator it = std::find_if(
        _targets.begin(), _targets.end(),
        [homeId, zoneId, deviceId, type](const DeviceTarget &target) {
            return target.matches( (const char *)homeId, (const char *)zoneId, (const char *)deviceId, (const char *)type );
        });

    if( it != _targets.end() ) {
        return &(*it);
    }

    return NULL;
}

Override::Override( cJSON *json, const char *defaultHomeId, const char *defaultZoneId )
    : _start( 0 ), _end( 0 )
{
    {
        cJSON *start = cJSON_GetObjectItem( json, "start" );
        if( start && cJSON_IsString( start ) ) {
            struct tm tmstart;
            strptime( start->valuestring, "%FT%TZ", &tmstart );
            _start = mktime( &tmstart ) - _timezone;
            _end = _start;
        }
    }

    {
        cJSON *end = cJSON_GetObjectItem( json, "end" );
        if( end && cJSON_IsString( end ) ) {
            struct tm tmend;
            strptime( end->valuestring, "%FT%TZ", &tmend );
            _end = mktime( &tmend ) - _timezone;
        }
    }

    {
        cJSON *changes = cJSON_GetObjectItem( json, "changes" );
        if( changes && cJSON_IsArray( changes ) ) {
            int numChanges = cJSON_GetArraySize( changes );
            for( int i = 0; i < numChanges; ++i ) {
                _targets.emplace_front( cJSON_GetArrayItem( changes, i ), defaultHomeId, defaultZoneId );
            }
        }
    }

    ESP_LOGI( TAG, "Created override for %ld to %ld", _start, _end );
}

Override::~Override()
{
}

const DeviceTarget* Override::getTarget( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const
{
    DeviceTargetList::const_iterator it = std::find_if(
        _targets.begin(), _targets.end(),
        [homeId, zoneId, deviceId, type](const DeviceTarget &target) {
            return target.matches( (const char *)homeId, (const char *)zoneId, (const char *)deviceId, (const char *)type );
        });

    if( it != _targets.end() ) {
        return &(*it);
    }

    return NULL;
}



DeviceTarget::DeviceTarget( cJSON *json, const char *defaultHomeId, const char *defaultZoneId )
{
    {
        cJSON *homeId = cJSON_GetObjectItem( json, "home" );
        if( homeId && cJSON_IsString( homeId ) ) {
            strncpy( _homeId, homeId->valuestring, sizeof( _homeId ) - 1 );
            _homeId[sizeof( _homeId ) - 1] = '\0';
        } else if( defaultHomeId ) {
            strncpy( _homeId, defaultHomeId, sizeof( _homeId ) - 1 );
            _homeId[sizeof( _homeId ) - 1] = '\0';
        } else {
            _homeId[0] = '\0';
        }
    }
    {
        cJSON *zoneId = cJSON_GetObjectItem( json, "zone" );
        if( zoneId && cJSON_IsString( zoneId ) ) {
            strncpy( _zoneId, zoneId->valuestring, sizeof( _zoneId ) - 1 );
            _zoneId[sizeof( _zoneId ) - 1] = '\0';
        } else if( defaultZoneId ) {
            strncpy( _zoneId, defaultZoneId, sizeof( _zoneId ) - 1 );
            _zoneId[sizeof( _zoneId ) - 1] = '\0';
        } else {
            _zoneId[0] = '\0';
        }
    }
    {
        cJSON *deviceId = cJSON_GetObjectItem( json, "device" );
        if( deviceId && cJSON_IsString( deviceId ) ) {
            strncpy( _deviceId, deviceId->valuestring, sizeof( _deviceId ) - 1 );
            _deviceId[sizeof( _deviceId ) - 1] = '\0';
        } else {
            _deviceId[0] = '\0';
        }
    }
    {
        cJSON *type = cJSON_GetObjectItem( json, "type" );
        if( type && cJSON_IsString( type ) ) {
            strncpy( _type, type->valuestring, sizeof( _type ) - 1 );
            _type[sizeof( _type ) - 1] = '\0';
        } else {
            _type[0] = '\0';
        }
    }
    {
        cJSON *value = cJSON_GetObjectItem( json, "value" );
        if( value && cJSON_IsObject( value ) ) {
            cJSON *v = cJSON_GetObjectItem( value, "value" );
            if( v && cJSON_IsBool( v ) ) {
                _value.value.boolValue = v->valueint != 0;
            } else if( v && cJSON_IsNumber( v ) ) {
                _value.value.doubleValue = v->valuedouble;
            } else {
                _value.value.doubleValue = 0;
            }

            cJSON *u = cJSON_GetObjectItem( value, "unit" );
            if( u && cJSON_IsString( u ) ) {
                strncpy( _value.unit, u->valuestring, sizeof( _value.unit ) - 1 );
                _value.unit[sizeof( _value.unit ) - 1] = '\0';
            } else {
                _value.unit[0] = '\0';
            }
        }
    }
}

bool DeviceTarget::matches( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const
{
    ESP_LOGI( TAG, "Checking %s == %s && %s == %s && %s == %s && %s == %s", _homeId, homeId, _zoneId, zoneId, _deviceId, deviceId, _type, type );
    return( strcmp( _homeId, homeId ) == 0 && strcmp( _zoneId, zoneId ) == 0 &&
            strcmp( _deviceId, deviceId ) == 0 && ( _type[0] == '\0' || strcmp( _type, type ) == 0 ) );
}

