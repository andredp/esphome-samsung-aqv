import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir
from esphome.const import CONF_ID

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@andredp"]

samsung_aqv_ns = cg.esphome_ns.namespace("samsung_aqv")
SamsungAqvClimate = samsung_aqv_ns.class_("SamsungAqvClimate", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.CLIMATE_IR_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(SamsungAqvClimate)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate_ir.register_climate_ir(var, config)
