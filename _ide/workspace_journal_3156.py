# 2026-06-20T10:39:30.425528700
import vitis

client = vitis.create_client()
client.set_workspace(path="final_project_eth_nexys_video")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="lwip_echo_server")
comp.build()

status = comp.clean()

status = platform.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

vitis.dispose()

