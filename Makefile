CC      = clang
RDIR    = $(shell brew --prefix raylib)
# IRONFIST_V2 enables the polish + map-engine changes from the v2 branch
# (postprocess shader, hit-stop, combo counter, footstep dust, edge
# vignettes, score popups, text-driven map loader, etc.) — it's the
# default in every target now. The legacy v1 codepath still compiles
# cleanly without the define for revert capability.
CFLAGS  = -O2 -Wall -Wno-unused-result -I$(RDIR)/include -DIRONFIST_V2 -Isrc/v2 -Isrc
# Foundation framework is needed by src/score_post.m (NSURLSession-based
# high-score POST). Native macOS only — the web build skips score_post.m
# and uses EM_JS fetch instead; the Windows target currently no-ops the
# submission.
LDFLAGS = -L$(RDIR)/lib -lraylib -lm \
          -framework OpenGL -framework Cocoa -framework IOKit \
          -framework CoreAudio -framework CoreVideo -framework AudioToolbox \
          -framework Foundation

APP     = IronFist3D.app
BIN     = $(APP)/Contents/MacOS/IronFist3D
ICNS    = $(APP)/Contents/Resources/icon.icns
SPRITES = $(APP)/Contents/Resources/sprites
SOUNDS  = $(APP)/Contents/Resources/sounds
LEVELS  = $(APP)/Contents/Resources/levels
SRC     = src/game.c src/hud.c src/effects.c src/v2/level.c src/v2/postfx.c
# Objective-C bridge for macOS-only NSURLSession HTTP POST.
SRC_M   = src/score_post.m

game: $(BIN) $(ICNS) $(APP)/Contents/Info.plist $(SPRITES) $(SOUNDS) $(LEVELS)
	@echo "Built $(APP) — run with: open $(APP)"

$(LEVELS): levels | $(APP)/Contents/Resources
	cp -r levels $(APP)/Contents/Resources/

$(APP)/Contents/MacOS:
	mkdir -p $(APP)/Contents/MacOS

$(APP)/Contents/Resources:
	mkdir -p $(APP)/Contents/Resources

$(BIN): $(SRC) $(SRC_M) | $(APP)/Contents/MacOS
	$(CC) $(CFLAGS) $(SRC) $(SRC_M) $(LDFLAGS) -o $(BIN)

$(APP)/Contents/Info.plist: | $(APP)/Contents/MacOS
	@echo '<?xml version="1.0" encoding="UTF-8"?>' > $@
	@echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> $@
	@echo '<plist version="1.0"><dict>' >> $@
	@echo '  <key>CFBundleName</key><string>Iron Fist 3D</string>' >> $@
	@echo '  <key>CFBundleDisplayName</key><string>Iron Fist 3D</string>' >> $@
	@echo '  <key>CFBundleIdentifier</key><string>com.ironfist.game</string>' >> $@
	@echo '  <key>CFBundleVersion</key><string>1.0</string>' >> $@
	@echo '  <key>CFBundleExecutable</key><string>IronFist3D</string>' >> $@
	@echo '  <key>CFBundleIconFile</key><string>icon</string>' >> $@
	@echo '  <key>NSHighResolutionCapable</key><true/>' >> $@
	@echo '  <key>LSMinimumSystemVersion</key><string>11.0</string>' >> $@
	@echo '</dict></plist>' >> $@

$(ICNS): | $(APP)/Contents/Resources
	python3 gen_icon.py
	mkdir -p /tmp/ironfist_icon.iconset
	sips -z 16 16     /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_16x16.png    >/dev/null
	sips -z 32 32     /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_16x16@2x.png >/dev/null
	sips -z 32 32     /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_32x32.png    >/dev/null
	sips -z 64 64     /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_32x32@2x.png >/dev/null
	sips -z 128 128   /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_128x128.png  >/dev/null
	sips -z 256 256   /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_128x128@2x.png >/dev/null
	sips -z 256 256   /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_256x256.png  >/dev/null
	sips -z 512 512   /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_256x256@2x.png >/dev/null
	sips -z 512 512   /tmp/ironfist.png --out /tmp/ironfist_icon.iconset/icon_512x512.png  >/dev/null
	iconutil -c icns /tmp/ironfist_icon.iconset -o $(ICNS)

$(SPRITES): sprites | $(APP)/Contents/Resources
	cp -r sprites $(APP)/Contents/Resources/

$(SOUNDS): sounds | $(APP)/Contents/Resources
	cp -r sounds $(APP)/Contents/Resources/

run: game
	open $(APP)

# ─── Windows cross-compile (mingw-w64) ──────────────────────────────────────
# brew install mingw-w64. raylib prebuilt lives at vendor/{include,lib}/.
# Output: a single self-contained dist-win/IronFist3D.exe with all sprites +
# sounds baked in as an RCDATA resource named "bundle". The exe extracts them
# to %TEMP%/IronFist3D/ on first launch.
WINCC   = x86_64-w64-mingw32-gcc
WINRES  = x86_64-w64-mingw32-windres
WINDIR  = dist-win
WINEXE  = $(WINDIR)/IronFist3D.exe
WINVENDOR = vendor
WINCFLAGS = -O2 -Wall -Wno-unused-result -I$(WINVENDOR)/include -DIRONFIST_V2 -Isrc/v2 -Isrc
WINLDFLAGS = -L$(WINVENDOR)/lib -lraylib -lopengl32 -lgdi32 -lwinmm \
             -static-libgcc -static-libstdc++ -Wl,-subsystem,windows

windows: $(WINEXE)
	@echo "Built $(WINEXE) — single self-contained exe"

$(WINDIR)/bundle.dat: pack_bundle.py sprites sounds | $(WINDIR)
	python3 pack_bundle.py $@ sprites sounds

$(WINDIR)/bundle.rc: | $(WINDIR)
	@printf 'bundle RCDATA "bundle.dat"\n' > $@

$(WINDIR)/bundle.o: $(WINDIR)/bundle.rc $(WINDIR)/bundle.dat
	cd $(WINDIR) && $(WINRES) -i bundle.rc -O coff -o bundle.o

$(WINEXE): $(SRC) $(WINDIR)/bundle.o | $(WINDIR)
	$(WINCC) $(WINCFLAGS) $(SRC) $(WINDIR)/bundle.o $(WINLDFLAGS) -o $(WINEXE)

$(WINDIR):
	mkdir -p $(WINDIR)

# ─── Web build (Emscripten / WebAssembly) ──────────────────────────────────
# Requires:
#   1. emsdk installed, activated, and env sourced:
#        git clone https://github.com/emscripten-core/emsdk vendor/emsdk
#        ./vendor/emsdk/emsdk install latest && ./vendor/emsdk/emsdk activate latest
#        source ./vendor/emsdk/emsdk_env.sh
#   2. raylib source cloned + built for PLATFORM_WEB (handled by `make web-raylib`):
#        make web-raylib      # clones into vendor/raylib-src/ and builds libraylib.a
#
# Then:
#   make web          # builds dist-web/index.html + .js + .wasm + .data
#   make web-serve    # builds, then serves dist-web/ on http://localhost:8000/
#
# The game uses pointer-lock for mouse look + Web Audio for sound; both need
# a user gesture, satisfied by the shell's "click to start" overlay.
WEBDIR       = dist-web
RAYLIB_SRC  ?= vendor/raylib-src
RAYLIB_WEB_A = $(RAYLIB_SRC)/src/libraylib.a
WEBSHELL     = web/shell.html

# GRAPHICS_API_OPENGL_ES3 pairs with WebGL 2, which supports the GLSL ES 300
# shaders we emit when PLATFORM_WEB is defined. USE_GLFW=3 is raylib's
# required windowing backend on web.
WEBCFLAGS = -O2 -Wall -Wno-unused-result \
            -I$(RAYLIB_SRC)/src \
            -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3 \
            -DIRONFIST_V2 -Isrc/v2 -Isrc
WEBLDFLAGS = -s USE_GLFW=3 \
             -s MIN_WEBGL_VERSION=2 -s MAX_WEBGL_VERSION=2 \
             -s FORCE_FILESYSTEM=1 \
             -s ALLOW_MEMORY_GROWTH=1 \
             -s INITIAL_MEMORY=128MB \
             -s STACK_SIZE=1MB \
             -s ASYNCIFY \
             -s "EXPORTED_FUNCTIONS=['_main','_IronFistRankReceived']" \
             --preload-file sprites \
             --preload-file sounds \
             --preload-file levels \
             --shell-file $(WEBSHELL)

web: $(WEBDIR)/index.html
	@echo "Built $(WEBDIR)/index.html — serve with: make web-serve"

$(WEBDIR):
	mkdir -p $(WEBDIR)

$(RAYLIB_SRC):
	@mkdir -p vendor
	git clone --depth 1 https://github.com/raysan5/raylib.git $@

$(RAYLIB_WEB_A): | $(RAYLIB_SRC)
	@command -v emcc >/dev/null || { echo "error: emcc not on PATH — source emsdk_env.sh first"; exit 1; }
	cd $(RAYLIB_SRC)/src && emmake $(MAKE) PLATFORM=PLATFORM_WEB GRAPHICS=GRAPHICS_API_OPENGL_ES3 -B

web-raylib: $(RAYLIB_WEB_A)

$(WEBDIR)/index.html: $(SRC) $(RAYLIB_WEB_A) $(WEBSHELL) sprites sounds | $(WEBDIR)
	@command -v emcc >/dev/null || { echo "error: emcc not on PATH — source emsdk_env.sh first"; exit 1; }
	emcc $(SRC) $(RAYLIB_WEB_A) $(WEBCFLAGS) $(WEBLDFLAGS) -o $@

web-serve: web
	@echo "Serving $(WEBDIR) at http://localhost:8000/ — Ctrl-C to stop"
	@cd $(WEBDIR) && python3 -m http.server 8000

web-clean:
	rm -rf $(WEBDIR)

clean:
	rm -rf $(APP) IronFist3D-v2.app $(WINDIR) $(WEBDIR) /tmp/ironfist.png /tmp/ironfist_icon.iconset
