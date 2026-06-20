# 2026-06-20T13:26:44.969999500
import vitis

client = vitis.create_client()
client.set_workspace(path="final_project_eth_nexys_video")

platform = client.get_component(name="platform")
status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = platform.build()

status = platform.build()

comp = client.get_component(name="lwip_echo_server")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

status = platform.build()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

domain = platform.get_domain(name="standalone_microblaze_0")

status = domain.regenerate()

status = domain.regenerate()

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

vitis.dispose()

