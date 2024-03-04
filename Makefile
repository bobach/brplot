# This are the configuration options.
# default configuration (calling make with 0 arguments) call is:
# $ make CONFIG=RELEASE PLATFORM=LINUX GUI=IMGUI TYPE=EXE COMPILER=GCC
# But you can change it by changing one param e.g. use clang:
# $ make COMPILER=CLANG

# DEBUG | RELEASE
CONFIG?= RELEASE
# LINUX | WEB | WINDOWS
PLATFORM?= LINUX
# IMGUI | RAYLIB | HEADLESS
GUI?= IMGUI
# EXE | LIB
TYPE?= EXE
# GCC | CLANG ( Only for linux build )
COMPILER?= GCC
# YES | NO
COVERAGE?= NO
# YES | NO
FUZZ?= NO

RL                 = ./external/raylib-5.0/src
IM                 = ./external/imgui-docking
RAYLIB_SOURCES     = $(RL)/rmodels.c $(RL)/rshapes.c $(RL)/rtext.c $(RL)/rtextures.c $(RL)/utils.c $(RL)/rcore.c
SOURCE             = src/main.c src/help.c src/points_group.c src/smol_mesh.c src/q.c src/read_input.c src/gui.c src/keybindings.c src/str.c src/memory.cpp src/resampling2.c src/graph_utils.c src/br_shaders.c
EXTERNAL_HEADERS   =
COMMONFLAGS        = -I. -MMD -MP
WARNING_FLAGS      = -Wconversion -Wall -Wpedantic -Wextra
LD_FLAGS           =
SHADERS_FOLDER     =

ifeq ($(PLATFORM)_$(COMPILER), LINUX_CLANG)
	ifeq ($(COVERAGE), YES)
		COMMONFLAGS+= -fprofile-instr-generate -fcoverage-mapping -mllvm -runtime-counter-relocation
	endif
	ifeq ($(FUZZ), YES)
		COMMONFLAGS+= -fsanitize=fuzzer
	endif
	WARNING_FLAGS+= -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-newline-eof
	CXX= clang++
	CC= clang
else ifeq ($(PLATFORM)_$(COMPILER), LINUX_GCC)
	CXX= g++
	CC= gcc
endif

# To debug include files use -H flag

ifeq ($(GUI), IMGUI)
	SOURCE+= $(IM)/imgui.cpp $(IM)/imgui_draw.cpp $(IM)/imgui_tables.cpp \
				  $(IM)/imgui_widgets.cpp $(IM)/backends/imgui_impl_glfw.cpp $(IM)/backends/imgui_impl_opengl3.cpp \
				  src/imgui/gui.cpp src/imgui/ui_settings.cpp src/imgui/ui_info.cpp src/imgui/imgui_extensions.cpp src/imgui/file_saver.cpp \
					$(RAYLIB_SOURCES)
	COMMONFLAGS+= -I$(IM) -I$(RL) -DIMGUI

else ifeq ($(GUI), RAYLIB)
	COMMONFLAGS+= -I$(RL)
	SOURCE+= src/raylib/gui.c src/raylib/ui.c $(RAYLIB_SOURCES)

else ifeq ($(GUI), HEADLESS)
	COMMONFLAGS+= -I$(RL)
	SOURCE+= src/headless/raylib_headless.c src/headless/gui.c
	PLATFORM= LINUX
	COMMONFLAGS+= -DNUMBER_OF_STEPS=100

else
	echo "Valid GUI parameters are IMGUI, RAYLIB, HEADLESS" && exit -1
endif

ifeq ($(PLATFORM), LINUX)
	LIBS= `pkg-config --static --libs glfw3` -lGL
	COMMONFLAGS+= -DLINUX=1 -DPLATFORM_DESKTOP=1
	SOURCE+= src/desktop/linux/read_input.c src/desktop/platform.c
	SHADERS_HEADER= src/misc/shaders.h
	SHADERS_FOLDER= src/desktop/shaders

else ifeq ($(PLATFORM), WINDOWS)
	LIBS= -lopengl32 -lgdi32 -lwinmm
	CXX= x86_64-w64-mingw32-g++
	CC= x86_64-w64-mingw32-gcc
	COMMONFLAGS+= -Iexternal/glfw/include -DWINDOWS=1 -DPLATFORM_DESKTOP=1 -D_WIN32=1 -DWIN32_LEAN_AND_MEAN
	SOURCE+= $(RL)/rglfw.c src/desktop/win/read_input.c src/desktop/platform.c
	SHADERS_HEADER= src/misc/shaders.h
	SHADERS_FOLDER= src/desktop/shaders
	COMPILER= MINGW

else ifeq ($(PLATFORM), WEB)
	CXX= $(EMSCRIPTEN)em++
	CC= $(EMSCRIPTEN)emcc
	COMMONFLAGS+= -DGRAPHICS_API_OPENGL_ES3=1 -DPLATFORM_WEB=1
	LD_FLAGS= -sWASM_BIGINT -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sGL_ENABLE_GET_PROC_ADDRESS --shell-file=src/web/minshell.html
	LD_FLAGS+= -sCHECK_NULL_WRITES=0 -sDISABLE_EXCEPTION_THROWING=1 -sFILESYSTEM=0 -sDYNAMIC_EXECUTION=0
	SOURCE+= src/web/read_input.c src/web/platform.c
	SHADERS_FOLDER= src/web/shaders
	SHADERS_HEADER= src/misc/shaders_web.h
	COMPILER= EMCC
	ifeq ($(TYPE), LIB)
		COMMONFLAGS+= -DLIB
		LD_FLAGS+= -sMODULARIZE=1 -sEXPORT_ES6=1
		OUTPUT= $(shell echo 'www/brplot_$(GUI)_$(CONFIG)_lib.js' | tr '[A-Z]' '[a-z]')
	else ifeq ($(TYPE), EXE)
		OUTPUT= $(shell echo 'www/brplot_$(GUI)_$(CONFIG).html' | tr '[A-Z]' '[a-z]')
		LD_FLAGS+= -sASYNCIFY
	else
		echo "Valid TYPE parameter values are LIB, EXE" && exit -1
	endif
else
	echo "Valid PLATFORM parameter values are LINUX, WINDOWS, WEB" && exit -1
endif

ifeq ($(CONFIG), DEBUG)
	COMMONFLAGS+= -g
	ifeq ($(PLATFORM), LINUX)
		SOURCE+= src/desktop/linux/refresh_shaders.c
		COMMONFLAGS+= -DUNIT_TEST
		ifeq ($(COMPILER), GCC)
		  COMMONFLAGS+= -fsanitize=bounds-strict
		endif
		ifeq ($(COVERAGE), NO)
			LD_FLAGS+= -rdynamic
			COMMONFLAGS+= -fpie -pg \
			 -fsanitize=address -fsanitize=leak \
			 -fsanitize=undefined -fsanitize=signed-integer-overflow \
			 -fsanitize=integer-divide-by-zero -fsanitize=shift -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow
		endif
	endif
	ifeq ($(PLATFORM), WINDOWS)
		SOURCE+= src/desktop/win/refresh_shaders.c
	endif
	ifeq ($(PLATFORM), WEB)
		SOURCE+= src/web/refresh_shaders.c
	endif
	ifeq ($(GUI), IMGUI)
		SOURCE+= $(IM)/imgui_demo.cpp
		ifeq ($(PLATFORM), LINUX)
			SOURCE+= src/imgui/hotreload.c
		endif
	endif
else ifeq ($(CONFIG), RELEASE)
	COMMONFLAGS+= -fdata-sections -ffunction-sections -Os -DRELEASE=1 \
		-DIMGUI_DISABLE_DEMO_WINDOWS \
		-DIMGUI_DISABLE_DEBUG_TOOLS
	LD_FLAGS+= -fdata-sections -ffunction-sections -Wl,--gc-sections 
	ifeq ($(PLATFORM), LINUX)
		LD_FLAGS+= -flto=auto
	endif
else
	$(error BadCONFIG)
endif



PREFIX_BUILD= $(shell echo 'build/$(PLATFORM)/$(CONFIG)/$(GUI)/$(COMPILER)' | tr '[A-Z]' '[a-z]')
OBJSA= $(patsubst %.cpp, $(PREFIX_BUILD)/%.o, $(SOURCE))
OBJS+= $(patsubst %.c, $(PREFIX_BUILD)/%.o, $(OBJSA))
MAKE_INCLUDES= $(patsubst %.o, %.d, $(OBJS))
CXXFLAGS= $(COMMONFLAGS) -fno-exceptions -std=gnu++17
CCFLAGS= $(COMMONFLAGS)
OUTPUT?= $(shell echo 'bin/brplot_$(GUI)_$(PLATFORM)_$(CONFIG)_$(COMPILER)' | tr '[A-Z]' '[a-z]')

OBJSDIR= $(sort $(dir $(OBJS)))
$(shell $(foreach var,$(OBJSDIR), test -d $(var) || mkdir -p $(var);))
$(shell test -d $(dir $(OUTPUT)) || mkdir $(dir $(OUTPUT)))
$(shell test -d bin || mkdir bin)

SHADERS_LIST= $(SHADERS_FOLDER)/grid_3d.fs $(SHADERS_FOLDER)/grid_3d.vs \
							$(SHADERS_FOLDER)/line_3d.fs $(SHADERS_FOLDER)/line_3d.vs \
							$(SHADERS_FOLDER)/line_3d_simple.fs $(SHADERS_FOLDER)/line_3d_simple.vs \
	            $(SHADERS_FOLDER)/grid.fs $(SHADERS_FOLDER)/grid.vs \
							$(SHADERS_FOLDER)/line.fs $(SHADERS_FOLDER)/line.vs \
	            $(SHADERS_FOLDER)/quad.fs $(SHADERS_FOLDER)/quad.vs

AUTOGENERATED_HEADERS= $(SHADERS_HEADER) src/misc/default_font.h

$(OUTPUT): $(OBJS)
	$(CXX) $(COMMONFLAGS) $(LD_FLAGS) -o $@ $(LIBS) $(OBJS) $(LIBS)

$(PREFIX_BUILD)/src/%.o:src/%.c
	$(CC) $(CCFLAGS) $(WARNING_FLAGS) -c -o $@ $<

$(PREFIX_BUILD)/src/%.o:src/%.cpp
	$(CXX) $(CXXFLAGS) $(WARNING_FLAGS) -c -o $@ $<

$(PREFIX_BUILD)/%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(PREFIX_BUILD)/%.o:%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

$(OBJS): $(AUTOGENERATED_HEADERS)

.PHONY: clean
clean:
	test -d build &&  rm build -rdf || echo "done"
	test -d bin &&  rm bin -rdf || echo "done"
	test -d www &&  rm www -rdf || echo "done"
	test -d zig-cache && rm zig-cache -rdf || echo "done"
	test -d zig-out && rm zig-out -rdf || echo "done"
	test -f src/misc/shaders.h && rm src/misc/shaders.h || echo "done"
	test -f src/misc/shaders_web.h && rm src/misc/shaders_web.h || echo "done"
	test -f src/misc/default_font.h && rm src/misc/default_font.h || echo "done"

.PHONY: fuzz
fuzz:
	make GUI=HEADLESS && \
	cat /dev/random | ./bin/brplot_headless_linux_debug_gcc > /dev/null && echo "Fuzz test OK"

.PHONY: test
test:
	make GUI=HEADLESS CONFIG=DEBUG && \
	./bin/brplot_headless_linux_debug_gcc --unittest

.PHONY: test-gdb
test-gdb:
	make GUI=HEADLESS CONFIG=DEBUG && \
	gdb -ex "r --unittest" ./bin/brplot_headless_linux_debug_gcc --tui

.PHONY: npm-imgui
npm-imgui:
	make GUI=IMGUI CONFIG=RELEASE TYPE=LIB PLATFORM=WEB && \
	cp ./www/brplot_imgui_release_lib.js packages/npm/brplot.js && \
	cp ./www/brplot_imgui_release_lib.wasm packages/npm && \
	  ((cd packages/npm && \
	   npm publish || cd ../..) && cd ../..)

bin/upper: tools/upper.cpp
	g++ -O3 -o bin/upper tools/upper.cpp

bin/lower: tools/lower.cpp
	g++ -O3 -o bin/lower tools/lower.cpp

src/misc/default_font.h: bin/font_export fonts/PlayfairDisplayRegular-ywLOY.ttf
	bin/font_export fonts/PlayfairDisplayRegular-ywLOY.ttf > src/misc/default_font.h

bin/font_export: tools/font_export.c
	gcc -o bin/font_export tools/font_export.c -lm

$(SHADERS_HEADER): $(SHADERS_LIST) bin/upper bin/lower
	echo "" > $(SHADERS_HEADER)
	echo "#pragma once" >> $(SHADERS_HEADER)
	echo "//This file is autogenerated" >> $(SHADERS_HEADER)
	$(foreach s, $(SHADERS_LIST), echo "#define" | bin/lower >> $(SHADERS_HEADER) && \
																echo 'SHADER_$(word 4, $(subst /, , $(s))) \' | sed 's/\./_/' | bin/upper >> $(SHADERS_HEADER) && \
																cat $(s) | sed 's/\(.*\)/"\1\\n" \\/' >> $(SHADERS_HEADER) && \
																echo "" >> $(SHADERS_HEADER) && ) echo "OKi"

COMPILE_FLAGS_JSONA= $(patsubst %.cpp, $(PREFIX_BUILD)/%.json, $(SOURCE))
COMPILE_FLAGS_JSON= $(patsubst %.c, $(PREFIX_BUILD)/%.json, $(COMPILE_FLAGS_JSONA))

PWD= $(shell pwd)

compile_commands.json: $(COMPILE_FLAGS_JSON)
	echo "[" > compile_commands.json
	cat $(COMPILE_FLAGS_JSON) >> compile_commands.json
	echo "]" >> compile_commands.json

$(PREFIX_BUILD)/src/%.json:src/%.c
	echo '{' > $@ && \
  echo '"directory": "$(PWD)",' >> $@ && \
  echo '"command": "$(CC) $(CCFLAGS) $(WARNING_FLAGS) -c $<",' >> $@ && \
  echo '"file": "$<"' >> $@ && \
	echo '},' >> $@

$(PREFIX_BUILD)/src/%.json:src/%.cpp
	echo '{' > $@ && \
  echo '"directory": "$(PWD)",' >> $@ && \
  echo '"command": "$(CXX) $(CXXFLAGS) $(WARNING_FLAGS) -c $<",' >> $@ && \
  echo '"file": "$<"' >> $@ && \
	echo '},' >> $@

$(PREFIX_BUILD)/%.json:%.c
	echo '{' > $@ && \
  echo '"directory": "$(PWD)",' >> $@ && \
  echo '"command": "$(CC) $(CCFLAGS) -c $<",' >> $@ && \
  echo '"file": "$<"' >> $@ && \
	echo '},' >> $@

$(PREFIX_BUILD)/%.json:%.cpp
	echo '{' > $@ && \
  echo '"directory": "$(PWD)",' >> $@ && \
  echo '"command": "$(CXX) $(CXXFLAGS) -c $<",' >> $@ && \
  echo '"file": "$<"' >> $@ && \
	echo '},' >> $@

-include $(MAKE_INCLUDES)
