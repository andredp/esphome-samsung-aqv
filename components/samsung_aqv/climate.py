import esphome.codegen as cg
from esphome.components import climate_ir

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@andredp"]

samsung_aqv_ns = cg.esphome_ns.namespace("samsung_aqv")
SamsungAqvClimate = samsung_aqv_ns.class_("SamsungAqvClimate", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(SamsungAqvClimate)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
