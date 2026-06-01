import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir, text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

AUTO_LOAD = ["climate_ir", "text_sensor"]
CODEOWNERS = ["@andredp"]

CONF_DEBUG = "debug"

samsung_aqv_ns = cg.esphome_ns.namespace("samsung_aqv")
SamsungAqvClimate = samsung_aqv_ns.class_("SamsungAqvClimate", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(SamsungAqvClimate).extend(
    {
        cv.Optional(CONF_DEBUG): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:remote",
        ),
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    if CONF_DEBUG in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DEBUG])
        cg.add(var.set_debug_sensor(sens))
