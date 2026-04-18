CC      = clang
RDIR    = $(shell brew --prefix raylib)
CFLAGS  = -O2 -Wall -Wno-unused-result -I$(RDIR)/include
LDFLAGS = -L$(RDIR)/lib -lraylib -lm \
          -framework OpenGL -framework Cocoa -framework IOKit \
          -framework CoreAudio -framework CoreVideo -framework AudioToolbox

APP     = IronFist3D.app
BIN     = $(APP)/Contents/MacOS/IronFist3D
ICNS    = $(APP)/Contents/Resources/icon.icns

game: $(BIN) $(ICNS) $(APP)/Contents/Info.plist
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

run: game
	open $(APP)

clean:
	rm -rf $(APP) /tmp/ironfist.png /tmp/ironfist_icon.iconset
