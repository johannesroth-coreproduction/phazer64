PROJECT = phazer
ROM_TITLE = "Phazer 64"

# Build configuration
# MASTER_BUILD: Release build with no FPS display
# RELEASE_BUILD: Release build with FPS display
# Both 0: Development build (all dev/debug flags can be used)
MASTER_BUILD ?= 1
RELEASE_BUILD ?= 0

# Ensure only one build type is set
ifneq ($(MASTER_BUILD)$(RELEASE_BUILD),00)
ifeq ($(MASTER_BUILD)$(RELEASE_BUILD),11)
$(error ERROR: Both MASTER_BUILD and RELEASE_BUILD cannot be set to 1. Set only one.)
endif
endif

BUILD_DIR = build
include $(N64_INST)/include/n64.mk

# Dev/debug flags (only enabled when both MASTER_BUILD and RELEASE_BUILD are 0)
ifeq ($(MASTER_BUILD)$(RELEASE_BUILD),00)
N64_CFLAGS += -DPROFILER_ENABLED    # Enable performance profiler
#N64_CFLAGS += -DSHOW_DETAILS        # Show detailed debug information
N64_CFLAGS += -DSAFE_COLLISSIONS    # Throws warnings if inactive/non-collidable entities are passed to collision functions
N64_CFLAGS += -DSKIP_EEPROM_INTEGRITY_CHECK  # Skip EEPROMFS signature check (dev only - allows code changes without wiping save)
N64_CFLAGS += -DDEV_BUILD            # Auto set by master/release 00
endif

# engine settings
N64_CFLAGS += -DLIBDRAGON_FAST_MATH

# FPS display (always active unless MASTER_BUILD=1)
ifneq ($(MASTER_BUILD),1)
N64_CFLAGS += -DSHOW_FPS
else
N64_CFLAGS += -DMASTER_BUILD
endif

src = $(wildcard *.c) $(wildcard game_objects/*.c) $(wildcard external/*.c) $(wildcard scripts/*.c)

# Auto-generate script registry from scripts/*.c files
scripts_registry = $(BUILD_DIR)/scripts_registry.inc
script_files = $(wildcard scripts/*.c)
script_names = $(patsubst scripts/%.c,%,$(script_files))
# Recursively find all PNG files in assets/ and subdirectories
assets_png = $(wildcard assets/*.png) $(wildcard assets/*/*.png) $(wildcard assets/*/*/*.png)
# Preserve directory structure: assets/planets_starfield/00.png -> filesystem/planets_starfield/00.sprite
assets_png_conv = $(patsubst assets/%.png,filesystem/%.sprite,$(assets_png))

# Recursively find all WAV files in assets/ and subdirectories
assets_wav = $(wildcard assets/*.wav) $(wildcard assets/*/*.wav) $(wildcard assets/*/*/*.wav)
# Preserve directory structure: assets/space/music.wav -> filesystem/space/music.wav64
assets_wav_conv = $(patsubst assets/%.wav,filesystem/%.wav64,$(assets_wav))

assets_csv = $(wildcard assets/*.csv) $(wildcard assets/*/*.csv) $(wildcard assets/*/*/*.csv)
assets_csv_conv = $(patsubst assets/%.csv,filesystem/%.csv,$(assets_csv))

AUDIOCONV_FLAGS ?=--wav-mono --wav-resample 22050 --wav-compress 1
MKSPRITE_FLAGS ?=

# List of sprite files that must be converted to RGBA16 format (WORKAROUND FOR CI4 bug)
RGBA16_SPRITES = race_pickup_00 race_finish_line_00 race_border_00 race_track_00 laser_beam_00 tractor_beam_00

# N64_ROM_REGION = P
N64_ROM_METADATA = rom_metadata/metadata.ini

all: $(PROJECT).z64

# Special rules for RGBA16 sprites (generated via foreach, must come before generic rule)
define RGBA16_RULE
filesystem/$(1).sprite: assets/$(1).png
	@mkdir -p $$(@D)
	@echo "    [SPRITE-RGBA16] $$@"
	@(cd "$$(@D)" && $$(N64_MKSPRITE) --format RGBA16 "$$(abspath $$<)")
endef
$(foreach sprite,$(RGBA16_SPRITES),$(eval $(call RGBA16_RULE,$(sprite))))

# Pattern rule that handles subdirectories: assets/subdir/file.png -> filesystem/subdir/file.sprite
filesystem/%.sprite: assets/%.png
	@mkdir -p $(@D)
	@echo "    [SPRITE] $@"
	@(cd "$(@D)" && $(N64_MKSPRITE) $(MKSPRITE_FLAGS) "$(abspath $<)")

# Special rule for intro_audio with seek points (must be before generic wav64 rule)
filesystem/intro_audio.wav64: assets/intro_audio.wav assets/intro_audio_seekpoints.txt
	@mkdir -p $(dir $@)
	@echo "    [AUDIO+SEEK] $@"
	@(cd "$(dir $@)" && $(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) --wav-seek "$(abspath assets/intro_audio_seekpoints.txt)" -o . "$(abspath $<)")
	
filesystem/%.wav64: assets/%.wav
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@(cd "$(dir $@)" && $(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o . "$(abspath $<)")

filesystem/%.csv: assets/%.csv
	@mkdir -p $(dir $@)
	@echo "    [CSV] $@"
	@cp $< $@

# Generate script registry file
$(scripts_registry): $(script_files) Makefile
	@mkdir -p $(dir $@)
	@echo "    [GEN] $@"
	@echo "/* Auto-generated script registry - DO NOT EDIT */" > $@
	@echo "/* Generated from script files in scripts directory */" >> $@
	@echo "" >> $@
	@echo "/* Forward declarations */" >> $@
	@$(foreach script,$(script_names),echo "ScriptInstance *script_$(script)(void);" >> $@;)
	@echo "" >> $@
	@echo "/* Script registry array */" >> $@
	@echo "static const script_registry_entry_t s_scriptRegistry[] = {" >> $@
	@$(foreach script,$(script_names),echo "    SCRIPT_REGISTER(\"$(script)\", script_$(script))," >> $@;)
	@echo "};" >> $@
	@echo "" >> $@
	@echo "#define SCRIPT_REGISTRY_COUNT (sizeof(s_scriptRegistry) / sizeof(s_scriptRegistry[0]))" >> $@

$(BUILD_DIR)/$(PROJECT).dfs: $(assets_wav_conv) $(assets_png_conv) $(assets_csv_conv)
$(BUILD_DIR)/script_handler.o: $(scripts_registry)
$(BUILD_DIR)/$(PROJECT).elf: $(src:%.c=$(BUILD_DIR)/%.o)

$(PROJECT).z64: N64_ROM_TITLE=$(ROM_TITLE)
$(PROJECT).z64: N64_ROM_SAVETYPE = eeprom4k
$(PROJECT).z64: $(BUILD_DIR)/$(PROJECT).dfs 

clean:
	rm -rf $(BUILD_DIR) filesystem $(PROJECT).z64

DEPS := $(src:%.c=$(BUILD_DIR)/%.d)
-include $(DEPS)

.PHONY: all clean