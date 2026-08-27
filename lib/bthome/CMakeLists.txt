zephyr_library()
zephyr_library_sources(bthome.c)

# the zephyr macros do not work with bt_le_ext_adv_update_param used, since it requires a lib.
# add_library(bthome_lib)
#
# target_link_libraries(bthome_lib PUBLIC
#   kernel
#   subsys__bluetooth__host
#   zephyr_interface
# )
# target_sources(bthome_lib PRIVATE
#   bthome.c
# )

# it should have been something like this:
# zephyr_library_link_libraries(subsys__bluetooth__host)
zephyr_linker_sources(DATA_SECTIONS bthome.ld)
