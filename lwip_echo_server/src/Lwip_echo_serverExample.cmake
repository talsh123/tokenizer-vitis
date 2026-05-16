set(AXIETHERNET_NUM_DRIVER_INSTANCES "axi_ethernet_0")
set(AXIETHERNET0_PROP_LIST "0x40c00000;0x2004")
list(APPEND TOTAL_AXIETHERNET_PROP_LIST AXIETHERNET0_PROP_LIST)
set(EMACLITE_NUM_DRIVER_INSTANCES "")
set(EMACPS_NUM_DRIVER_INSTANCES "")
set(TMRCTR_NUM_DRIVER_INSTANCES "axi_timer_0")
set(TMRCTR0_PROP_LIST "0x41c00000;0x2000")
list(APPEND TOTAL_TMRCTR_PROP_LIST TMRCTR0_PROP_LIST)
set(TTCPS_NUM_DRIVER_INSTANCES "")
set(mig_7series_0_memory_0 "0x80000000;0x20000000")
set(microblaze_0_local_memory_dlmb_bram_if_cntlr_memory_0 "0x50;0x1ffb0")
set(DDR mig_7series_0_memory_0)
set(CODE mig_7series_0_memory_0)
set(DATA mig_7series_0_memory_0)
set(TOTAL_MEM_CONTROLLERS "mig_7series_0_memory_0;microblaze_0_local_memory_dlmb_bram_if_cntlr_memory_0")
set(MEMORY_SECTION "MEMORY
{
	mig_7series_0_memory_0 : ORIGIN = 0x80000000, LENGTH = 0x20000000
	microblaze_0_local_memory_dlmb_bram_if_cntlr_memory_0 : ORIGIN = 0x50, LENGTH = 0x1ffb0
}")
set(STACK_SIZE 0xa000)
set(HEAP_SIZE 0xa000)
