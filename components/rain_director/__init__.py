import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    UNIT_PERCENT,
    ICON_WATER_PERCENT,
    STATE_CLASS_MEASUREMENT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor"]

CONF_TANK_LEVEL = "tank_level"
CONF_MODE_CODE = "mode_code"
CONF_STATE_CODE = "state_code"
CONF_MODE_TEXT = "mode"
CONF_STATUS_TEXT = "status"
CONF_SOURCE_TEXT = "source"

rain_director_ns = cg.esphome_ns.namespace("rain_director")
RainDirectorComponent = rain_director_ns.class_(
    "RainDirectorComponent", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RainDirectorComponent),
        cv.Optional(CONF_TANK_LEVEL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            icon=ICON_WATER_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_MODE_CODE): sensor.sensor_schema(
            icon="mdi:cog",
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_STATE_CODE): sensor.sensor_schema(
            icon="mdi:state-machine",
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_MODE_TEXT): text_sensor.text_sensor_schema(
            icon="mdi:water-pump",
        ),
        cv.Optional(CONF_STATUS_TEXT): text_sensor.text_sensor_schema(
            icon="mdi:progress-clock",
        ),
        cv.Optional(CONF_SOURCE_TEXT): text_sensor.text_sensor_schema(
            icon="mdi:water",
        ),
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_TANK_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_TANK_LEVEL])
        cg.add(var.set_tank_level_sensor(sens))

    if CONF_MODE_CODE in config:
        sens = await sensor.new_sensor(config[CONF_MODE_CODE])
        cg.add(var.set_mode_code_sensor(sens))

    if CONF_STATE_CODE in config:
        sens = await sensor.new_sensor(config[CONF_STATE_CODE])
        cg.add(var.set_state_code_sensor(sens))

    if CONF_MODE_TEXT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_MODE_TEXT])
        cg.add(var.set_mode_text_sensor(sens))

    if CONF_STATUS_TEXT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS_TEXT])
        cg.add(var.set_status_text_sensor(sens))

    if CONF_SOURCE_TEXT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_SOURCE_TEXT])
        cg.add(var.set_source_text_sensor(sens))
