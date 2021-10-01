#include "autohome.h"
#include <string.h>
#include <stdlib.h>
#include <algorithm>

static const char *TAG = "device";

Device::Device( Zone &zone, const char *id )
    : _zone( zone )
{
    if( id ) {
        strncpy( _id, id, sizeof( _id ) - 1 );
        _id[sizeof( _id ) - 1] = '\0';
    } else {
        _id[0] = '\0';
    }
}

Device::~Device()
{
}

Zone &Device::getZone() const
{
    return _zone;
}

const char *Device::getId() const
{
    return _id;
}

void Device::addChange( cJSON *json )
{
    _changes.emplace_front( json, _zone.getHomeId(), _zone.getZoneId() );
}

void Device::clearChanges()
{
    _changes.clear();
}

void Device::addCalibration( cJSON *json )
{
    _calibrations.emplace_front( json );
}

void Device::clearCalibrations()
{
    _calibrations.clear();
}

const DeviceCalibration* Device::findCalibration( const char *type )
{
    DeviceCalibrationList::iterator it = std::find_if(
        _calibrations.begin(), _calibrations.end(),
        [type](const DeviceCalibration &calibration) {
            return calibration.matches( (const char *)type );
        });

    if( it != _calibrations.end() ) {
        return &(*it);
    }

    return NULL;
}

DeviceChange::DeviceChange( cJSON *json, const char *defaultHomeId, const char *defaultZoneId )
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
        cJSON *direction = cJSON_GetObjectItem( json, "direction" );
        if( direction && cJSON_IsString( direction ) ) {
            if( strcmp( direction->valuestring, "increase" ) == 0 ) {
                _direction = 1;
            } else if( strcmp( direction->valuestring, "decrease" ) == 0 ) {
                _direction = -1;
            }
        }
    }
    ESP_LOGI( TAG, "Created new change for home %s zone %s device %s type %s direction %d", _homeId, _zoneId, _deviceId, _type, _direction );
}

bool DeviceChange::matches( const char *homeId, const char *zoneId, const char *deviceId, const char *type ) const
{
    ESP_LOGI( TAG, "Checking change %s %s %s %s -> %s %s %s %s", _homeId, _zoneId, _deviceId, _type, homeId, zoneId, deviceId, type );
    return( strcmp( _homeId, homeId ) == 0 && strcmp( _zoneId, zoneId ) == 0 &&
            strcmp( _deviceId, deviceId ) == 0 && ( _type[0] == '\0' || strcmp( _type, type ) == 0 ) );
}

DeviceCalibration::DeviceCalibration( cJSON *json )
{
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
        cJSON *value = cJSON_GetObjectItem( json, "calibration" );
        if( value && cJSON_IsObject( value ) ) {
            cJSON *v = cJSON_GetObjectItem( value, "value" );
            if( v && cJSON_IsBool( v ) ) {
                _calibration.value.boolValue = v->valueint != 0;
            } else if( v && cJSON_IsNumber( v ) ) {
                _calibration.value.doubleValue = v->valuedouble;
            } else {
                _calibration.value.doubleValue = 0;
            }

            cJSON *u = cJSON_GetObjectItem( value, "unit" );
            if( u && cJSON_IsString( u ) ) {
                strncpy( _calibration.unit, u->valuestring, sizeof( _calibration.unit ) - 1 );
                _calibration.unit[sizeof( _calibration.unit ) - 1] = '\0';
            } else {
                _calibration.unit[0] = '\0';
            }
        }
    }
    {
        cJSON *value = cJSON_GetObjectItem( json, "threshold" );
        if( value && cJSON_IsObject( value ) ) {
            cJSON *v = cJSON_GetObjectItem( value, "value" );
            if( v && cJSON_IsBool( v ) ) {
                _threshold.value.boolValue = v->valueint != 0;
            } else if( v && cJSON_IsNumber( v ) ) {
                _threshold.value.doubleValue = v->valuedouble;
            } else {
                _threshold.value.doubleValue = 0;
            }

            cJSON *u = cJSON_GetObjectItem( value, "unit" );
            if( u && cJSON_IsString( u ) ) {
                strncpy( _calibration.unit, u->valuestring, sizeof( _threshold.unit ) - 1 );
                _threshold.unit[sizeof( _threshold.unit ) - 1] = '\0';
            } else {
                _threshold.unit[0] = '\0';
            }
        }
    }
    ESP_LOGI( TAG, "Created new calibration for type %s", _type );
}

bool DeviceCalibration::matches( const char *type ) const
{
    ESP_LOGI( TAG, "Checking calibration %s -> %s", _type, type );
    return( strcmp( _type, type ) == 0 );
}

double DeviceCalibration::adjust( double value ) const
{
    return value + _calibration.value.doubleValue;
}

int DeviceCalibration::adjust( int value ) const
{
    return value + _calibration.value.intValue;
}

bool DeviceCalibration::adjust( bool value ) const
{
    // not sure what this means, really
    return value;
}
