var Gpio = require( 'onoff' ).Gpio;
var ds18x20 = require( 'ds18x20' );
var config = require( 'config' );
var superagent = require( 'superagent' );

async function setGPIO( output, on ) {
    console.log( `Turning ${ on ? 'ON' : 'OFF' } GPIO ${output._gpio}` );

    await new Promise( ( resolve, reject ) => {
        output.write( on ? Gpio.LOW : Gpio.HIGH, ( err, result ) => {
            if( err ) {
                return reject( err );
            }
            return resolve( result );
        });
    });
}

async function readDS18x20( id ) {
    return new Promise( ( resolve, reject ) => {
        ds18x20.get( id, ( err, result ) => {
            if( err ) {
                return reject( err );
            }
            return resolve( result );
        });
    });
}

async function readInput( input ) {
    try {
        input.value = null;
        if( input.interface === 'ds18x20' ) {
            input.value = ( await readDS18x20( input.pin ) ) + ( input.calibrate || 0 );
        }
        console.log( `Current value from ${input.name}: ${input.value}` );
    } catch( e ) {
        console.error( `Error reading input ${input.name}: ${e.message}` );
    }

    return input.value;
}

async function loadZone( zone ) {
    let etag = zone['@etag'];

    if( config.autohome ) {
        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}`;

            Object.assign( zone,  ( await superagent.get( url ).query({ api_key: config.autohome.api_key }) ).body );
        } catch( e ) {
            console.error( `Error updating config for zone ${zone.name} from autohome: ${e.message}` );
        }
    }


    if( !etag || etag !== zone['@etag'] ) {
        for( let output of zone.outputs ) {
            if( !output.gpio && output.interface === 'gpio' ) {
                output.gpio = new Gpio( output.pin, 'out' );
            }
        }

        zone.schedules = zone.schedules || [];
        zone.schedules.sort( ( a, b ) => {
            return a.start.localeCompare( b.start );
        });

        zone.overrides = zone.overrides || [];
        zone.overrides.sort( ( a, b ) => {
            return a.start.localeCompare( b.start );
        });

        zone['@etag'] = zone['@etag'] || 'initialized';
    }

    zone.changes = {};
    let now = new Date();
    let day = now.getDay();
    let time = now.toTimeString().slice( 0, 5 );

    for( let schedule of zone.schedules ) {
        if( schedule.days.indexOf( now.getDay() ) > -1 ) {
            if( schedule.start <= time ) {
                for( let change of schedule.changes ) {
                    zone.changes[change.type] = change;
                }
            } else {
                break;
            }
        }
    }

    for( let override of zone.overrides ) {
        if( new Date( override.start ) <= now && new Date( override.end ) >= now ) {
            for( let change of override.changes ) {
                zone.changes[change.type] = change;
            }
        }
    }
}

async function addSensorReading( zone, sensor, type, value, unit, data ) {
    if( config.autohome ) {
        try {
            let url = `${config.autohome.url}/api/homes/${config.autohome.home}/zones/${zone.id}/addSensorReading`;

            await superagent.post( url ).query({ api_key: config.autohome.api_key }).send({
                time: new Date().toISOString(),
                sensor,
                type,
                value,
                unit,
                data
            });
        } catch( e ) {
            console.error( `Error uploading sensor reading for ${sensor}: ${e.message}` );
        }
    }
}


async function update( reset ) {
    for( let zone of config.zones ) {
        await loadZone( zone );

        zone.readings = {};
        for( let input of( zone.inputs || [] ) ) {
            await readInput( input );
            if( input.value != null ) {
                zone.readings[input.type] = input.value;
                await addSensorReading( zone, input.name, input.type, input.value, input.unit );
            }
        }

        for( let output of( zone.outputs || [] ) ) {
            let target = zone.changes[output.type].value;
            let reading = zone.readings[output.type];
            if( target == null ) {
                console.warn( `No target ${output.type} specified` );
                continue;
            }

            console.log( `Scheduled ${output.type} is ${target}` );
            if( reading <= ( target - ( output.threshold || 0 ) ) ) {
                if( output.gpio ) {
                    await setGPIO( output.gpio, true );
                }
                await addSensorReading( zone, output.name, 'on-off', 1 );
            } else if( reset || reading >= ( target + ( output.threshold || 0 ) ) ) {
                if( output.gpio ) {
                    await setGPIO( output.gpio, false );
                }
                await addSensorReading( zone, output.name, 'on-off', 0 );
            }
        }
    }
}

update( true ).then( () => {
    setInterval( update, 60000 );
});

async function exitHandler( options, exitCode ) {
    for( let zone of config.zones ) {
        for( let output of zone.outputs ) {
            if( output.gpio ) {
                try {
                    await setGPIO( output.gpio, false );
                } catch( e ) {
                    console.error( e.stack );
                }
            }
        }
    }

    process.exit( exitCode || 0 );
}

// try to ensure the panel is off when we exit
process.on( 'exit', exitHandler.bind( null, {}) );
process.on( 'SIGINT', exitHandler.bind( null, {}) );
process.on( 'SIGUSR1', exitHandler.bind( null, {}) );
process.on( 'SIGUSR2', exitHandler.bind( null, {}) );
process.on( 'uncaughtException', exitHandler.bind( null, {}) );
