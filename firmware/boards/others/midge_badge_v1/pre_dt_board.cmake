# Suppress "unique_unit_address_if_enabled" to handle the following overlaps:
# - power@40000000 & clock@40000000 & bprot@40000000
# - acl@4001e000 & flash-controller@4001e000
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")

# Force support for 1280K PDM Clock which is required for sampling at 20KHz
add_compile_definitions(PDM_PDMCLKCTRL_FREQ_1280K=0x0A000000UL)
