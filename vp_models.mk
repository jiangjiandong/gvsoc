VP_BUILD_DIR = $(ROOT_VP_BUILD_DIR)/models

VP_INSTALL_PATH ?= $(PULP_SDK_WS_INSTALL)/lib
VP_PY_INSTALL_PATH ?= $(PULP_SDK_WS_INSTALL)/python

VP_MAKEFILE_LIST = $(addsuffix /Makefile,$(VP_DIRS))

CPP=g++

VP_COMP_PYBIND_FLAGS := $(shell python3-config --includes)

VP_COMP_CFLAGS=-MMD -MP -O2 -g -fpic -std=c++11 -I$(PULP_SDK_WS_INSTALL)/include $(VP_COMP_PYBIND_FLAGS)
VP_COMP_LDFLAGS=-O2 -g -shared -L$(PULP_SDK_WS_INSTALL)/lib -lpulpvp

VP_COMP_CFLAGS += -Werror -Wfatal-errors
VP_COMP_LDFLAGS += -Werror -Wfatal-errors

export PYTHONPATH := $(VP_INSTALL_PATH):$(PULP_SDK_WS_INSTALL)/lib:$(PYTHONPATH)







VP_COMP_EXT := .so
#$(shell python3-config --extension-suffix)

include $(VP_MAKEFILE_LIST)


define declare_implementation

-include $(VP_BUILD_DIR)/$(1).d

$(VP_BUILD_DIR)/$(1)$(VP_COMP_EXT): $($(1)_SRCS) $($(1)_DEPS)
	@mkdir -p `dirname $$@`
	$(CPP) $($(1)_SRCS) -o $$@ $($(1)_CFLAGS) $(VP_COMP_CFLAGS) $($(1)_LDFLAGS) $(VP_COMP_LDFLAGS)

$(VP_PY_INSTALL_PATH)/$(1)$(VP_COMP_EXT): $(VP_BUILD_DIR)/$(1)$(VP_COMP_EXT)
	install -D $$^ $$@

VP_INSTALL_TARGETS += $(VP_PY_INSTALL_PATH)/$(1)$(VP_COMP_EXT)

endef

define declare_component

$(VP_PY_INSTALL_PATH)/$(1).py: $(1).py
	install -D $$^ $$@

VP_INSTALL_TARGETS += $(VP_PY_INSTALL_PATH)/$(1).py

endef


$(foreach implementation, $(IMPLEMENTATIONS), $(eval $(call declare_implementation,$(implementation))))

$(foreach component, $(COMPONENTS), $(eval $(call declare_component,$(component))))


vp_build: $(VP_INSTALL_TARGETS)
	find $(VP_PY_INSTALL_PATH) -type d -exec touch {}/__init__.py \;

vp_clean:
	rm -rf $(VP_BUILD_DIR)