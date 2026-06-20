# 2026-06-19T13:48:56.333447
import vitis

client = vitis.create_client()
client.set_workspace(path="final_project_eth_nexys_video")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="lwip_echo_server")
comp.build()

