.DEFAULT_GOAL := all
TESTS := z-freeze vertex-skip
BUILD := build

.PHONY: all clean $(TESTS)
clean:
	rm -rf $(BUILD)

ifeq ($(strip $(DEVKITPPC)),)
DEVKIT_IMAGE ?= devkitpro/devkitppc:latest
all $(TESTS):
	docker run --rm --user "$$(id -u):$$(id -g)" \
		-v "$(CURDIR):/work:Z" -w /work $(DEVKIT_IMAGE) make $@
else
include $(DEVKITPPC)/gamecube_rules

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror $(MACHDEP) -I$(LIBOGC_INC)
LDFLAGS = -g $(MACHDEP) -Wl,-Map,$(@:.elf=.map)
LIBPATHS := -L$(LIBOGC_LIB)
LIBS := -logc -lm
LD := $(CC)

all: $(TESTS)
$(TESTS): %: $(BUILD)/%.dol

$(BUILD):
	mkdir -p $@

$(TESTS:%=$(BUILD)/%.o): $(BUILD)/%.o: %/source/main.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TESTS:%=$(BUILD)/%.elf): $(BUILD)/%.elf: $(BUILD)/%.o
	$(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@

.SECONDARY:
-include $(TESTS:%=$(BUILD)/%.d)
endif
