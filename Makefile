ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "Please create an environment variable PVSNESLIB_HOME with path to its folder and restart application.")
endif

CC  = $(PVSNESLIB_HOME)/devkitsnes/bin/816-tcc
AS  = $(PVSNESLIB_HOME)/devkitsnes/bin/wla-65816
LD  = $(PVSNESLIB_HOME)/devkitsnes/bin/wlalink
OPT = $(PVSNESLIB_HOME)/devkitsnes/bin/816-opt.py
CTF = $(PVSNESLIB_HOME)/devkitsnes/bin/constify

GFXCONV = $(PVSNESLIB_HOME)/devkitsnes/tools/gfx2snes
SMCONV  = $(PVSNESLIB_HOME)/devkitsnes/tools/smconv
BRCONV  = $(PVSNESLIB_HOME)/devkitsnes/tools/snesbrr
TXCONV  = $(PVSNESLIB_HOME)/devkitsnes/tools/bin2txt
SNTOOLS = $(PVSNESLIB_HOME)/devkitsnes/tools/snestools
TMXCONV = $(PVSNESLIB_HOME)/devkitsnes/tools/tmx2snes

CFLAGS += -I$(PVSNESLIB_HOME)/pvsneslib/include -I$(PVSNESLIB_HOME)/devkitsnes/include -Isrc -Ibin

GCC_ANALYZER = gcc -Wall -Wextra -Wshadow -Wformat -Wconversion -Wno-int-conversion -fanalyzer -nostdinc -isystem $(PVSNESLIB_HOME)/pvsneslib/include -isystem $(PVSNESLIB_HOME)/devkitsnes/include -Isrc -Ibin -D__int8_t_defined
# using AVR too simulate an 8-bit CPU architecture
CLANG_FLAGS = --target=avr -mmcu=atmega16 -Wall -Wextra -Wshadow -Wformat -Wconversion -nostdinc -isystem $(PVSNESLIB_HOME)/pvsneslib/include -isystem $(PVSNESLIB_HOME)/devkitsnes/include -Isrc -Ibin -D__int8_t_defined
CLANG_ANALYZER = clang-tidy -checks=-*,clang-analyzer-*

ifeq ($(strip $(NDEBUG)),1)
 CFLAGS += -DNDEBUG
 GCC_ANALYZER += -DNDEBUG
 CLANG_FLAGS += -DNDEBUG
endif
ifeq ($(strip $(HAS_BGM)),1)
 CFLAGS += -DHAS_BGM
 GCC_ANALYZER += -DHAS_BGM
 CLANG_FLAGS += -DHAS_BGM
 AS_FLAGS += -D HAS_BGM
endif
ifeq ($(strip $(HAS_SFX)),1)
 CFLAGS += -DHAS_SFX
 GCC_ANALYZER += -DHAS_SFX
 CLANG_FLAGS += -DHAS_SFX
 AS_FLAGS += -D HAS_SFX
endif
ifeq ($(strip $(USE_NTSC)),1)
 CFLAGS += -DUSE_NTSC
 GCC_ANALYZER += -DUSE_NTSC
 CLANG_FLAGS += -DUSE_NTSC
 AS_FLAGS += -D USE_NTSC
endif

ROMNAME = BombNBrake

SRC = \
	src/data.asm \
	src/hdr.asm \
	src/debug.asm \
	src/utility.asm \
	src/main.c \

DATA_DEP = \
	src/data.asm \
	bin/bg1.chr \
	bin/bg1.pal \
	bin/bg1.map \
	res/bg2.map \
	bin/fg1.chr \
	bin/fg1.pal \
	bin/fg1.map \
	bin/fg2.chr \
	bin/fg2.pal \
	bin/p12.chr \
	bin/p12.pal \
	res/field.map \
	res/options.map

MAIN_DEP = \
	src/main.c

ifeq ($(strip $(HAS_BGM)),1)
 SRC += bin/bgm1.asm
 MAIN_DEP += bin/bgm1.asm
 DATA_DEP += bin/bgm1.o
endif
ifeq ($(strip $(HAS_SFX)),1)
 DATA_DEP += bin/sfx1.brr
endif

OBJ = $(patsubst src/%,bin/%,$(patsubst %.c,%.o,$(patsubst %.asm,%.o,$(SRC))))
LIBDIRSOBJS = $(wildcard $(PVSNESLIB_HOME)/pvsneslib/lib/*.obj)

#---------------------------------------------------------------------------------
# on windows, linkfile can only manage path like E:\pvsneslib\lib\crt0_snes.obj
# this one doesn't work /e/pvsneslib/lib/crt0_snes.obj
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
 REPLPATH = sed 's|^/\(.\)/|\1:/|g'
else
 REPLPATH = cat
endif

all: bin/$(ROMNAME).sfc

.PHONY: clean
clean:
	rm -f bin/* res/*.pic

$(OBJ): |bin
.PHONY: bin
bin:
	mkdir -p bin

bin/$(ROMNAME).sfc: $(OBJ)
	# create linkfile
	@echo [objects] > bin/linkfile
	@for file in $(OBJ) $(LIBDIRSOBJS); do \
		echo $$file | $(REPLPATH) >> bin/linkfile; \
	done
	# link
	$(LD) -d -s -v -A bin/linkfile $(@)
	@sed -i 's/://' bin/$(ROMNAME).sym

.PHONY: gcc-analysis
gcc-analyzer: $(MAIN_DEP)
	$(GCC_ANALYZER) -c -o bin/gcc-analysis.o src/main.c

.PHONY: clang-analysis
clang-analyzer: $(MAIN_DEP)
	$(CLANG_ANALYZER) src/main.c -- $(CLANG_FLAGS)
	clang $(CLANG_FLAGS) -c -o bin/clang-main.o src/main.c

# C -> OBJ
bin/%.o: src/%.c
	# compile
	$(CC) $(CFLAGS) -Wall -Wunsupported -c $(<) -o bin/$(*).ps
	# optimize
	$(OPT) bin/$(*).ps >bin/$(*).asp
	# move constants
	$(CTF) $(<) bin/$(*).asp bin/$(*).asm
	# build
	$(AS) $(AS_FLAGS) -Ibin -Isrc -d -s -x -o $(@) bin/$(*).asm

# ASM -> OBJ
bin/%.o: src/%.asm
	# build
	$(AS) $(AS_FLAGS) -I src -I res -I bin -d -s -x -o $(@) $(<)

# ASM -> OBJ
bin/%.o: bin/%.asm
	# build
	$(AS) $(AS_FLAGS) -I src -I res -I bin -d -s -x -o $(@) $(<)

# BMP (256 colors) -> CHR (16 colors)
bin/bg1.chr bin/bg1.pal bin/bg1.map: res/bg1.bmp
	# convert bitmap
	$(GFXCONV) -n -gb -gs8 -po16 -pc16 -pe1 -m32p $(<)
	mv $(<:%.bmp=%.pic) bin/$(<F:%.bmp=%.chr)
	mv $(<:%.bmp=%.pal) bin/
	mv $(<:%.bmp=%.map) bin/

# BMP (256 colors) -> CHR (16 colors)
bin/fg1.chr bin/fg1.pal bin/fg1.map: res/fg1.bmp
	# convert bitmap
	$(GFXCONV) -n -gb -gs8 -po16 -pc16 -pe2 -mp -m32p $(<)
	mv $(<:%.bmp=%.pic) bin/$(<F:%.bmp=%.chr)
	mv $(<:%.bmp=%.pal) bin/
	mv $(<:%.bmp=%.map) bin/

# BMP (256 colors) -> CHR (32 colors)
bin/fg2.chr bin/fg2.pal: res/fg2.bmp
	# convert bitmap
	$(GFXCONV) -n -gs8 -po32 -pc16 -pe3 -mR! -m! $(<)
	mv $(<:%.bmp=%.pic) bin/$(<F:%.bmp=%.chr)
	mv $(<:%.bmp=%.pal) bin/

# BMP (256 colors) -> CHR (32 colors)
bin/p12.chr bin/p12.pal: res/p12.bmp
	# convert bitmap
	$(GFXCONV) -n -gs16 -po32 -pc16 -pe4 $(<)
	mv $(<:%.bmp=%.pic) bin/$(<F:%.bmp=%.chr)
	mv $(<:%.bmp=%.pal) bin/

# IT -> SPC
bin/bgm1.asm: res/bgm1.it
	# convert soundtrack
	$(SMCONV) -s -o bin/bgm1 -v -b 5 $(<)

# WAV -> BRR
bin/sfx1.brr: res/sfx1.wav
	# convert soundtrack
	$(BRCONV) -e $(<) $(@)

# dependencies
bin/data.o: $(DATA_DEP)
