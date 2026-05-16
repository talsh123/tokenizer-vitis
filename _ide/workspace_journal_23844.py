# 2026-04-10T21:11:41.152778900
import vitis

client = vitis.create_client()
client.set_workspace(path="final_project_eth_nexys_video")

platform = client.get_component(name="platform")
status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../design_1_wrapper.xsa")

comp = client.get_component(name="lwip_echo_server")
status = comp.clean()

domain = platform.get_domain(name="standalone_microblaze_0")

status = domain.regenerate()

status = platform.build()

status = platform.build()

comp.build()

vitis.dispose()

