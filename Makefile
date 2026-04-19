CC      = clang
RDIR    = $(shell brew --prefix raylib)
CFLAGS  = -O2 -Wall -Wno-unused-result -I$(RDIR)/include
LDFLAGS = -L$(RDIR)/lib -lraylib -lm \
          -framework OpenGL -framework Cocoa -framework IOKit \
          -framework CoreAudio -framework CoreVideo -framework AudioToolbox

APP     = IronFist3D.app
BIN     = $(APP)/Contents/MacOS/IronFist3D
ICNS    = $(APP)/Contents/Resources/icon.icns
SPRITES = $(APP)/Contents/Resources/sprites
SOUNDS  = $(APP)/Contents/Resources/sounds

game: $(BIN) $(ICNS) $(APP)/Contents/Info.plist $(SPRITES) $(SOUNDS)
	@echo "Built $(APP) — run with: open $(APP)"

$(APP)/Contents/MacOS:
	mkdir -p $(APP)/Contents/MacOS

$(APP)/Contents/Resources:
	mkdir -p $(APP)/Contents/Resources

$(BIN): game.c | $(APP)/Contents/MacOS
	$(CC) $(CFLAGS) game.c $(LDFLAGS) -o $(BIN)

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
WINCFLAGS = -O2 -Wall -Wno-unused-result -I$(WINVENDOR)/include
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

$(WINEXE): game.c $(WINDIR)/bundle.o | $(WINDIR)
	$(WINCC) $(WINCFLAGS) game.c $(WINDIR)/bundle.o $(WINLDFLAGS) -o $(WINEXE)

$(WINDIR):
	mkdir -p $(WINDIR)

clean:
	rm -rf $(APP) $(WINDIR) /tmp/ironfist.png /tmp/ironfist_icon.iconset
