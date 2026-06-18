# 2026-05-16T13:14:25.131662700
import vitis

client = vitis.create_client()
client.set_workspace(path="final_project_eth_nexys_video")

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

domain = platform.get_domain(name="standalone_microblaze_0")

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

vitis.dispose()

