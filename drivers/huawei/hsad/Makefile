
obj-y += config_interface.o config_mgr.o hardware_self_adapt.o config_debugfs.o
GET_LOCAL_DIR = $(patsubst %/, %, $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))))
$(shell cd $(GET_LOCAL_DIR);perl parse_product_id.pl ./product_boardid.xml)
