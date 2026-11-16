.DEFAULT_GOAL := build
GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),build)

.PHONY: $(GOALS) forward
$(GOALS): forward ;

forward:
	+$(MAKE) -C src $(GOALS) $(if $(FILE),FILE="$(abspath $(FILE))")
