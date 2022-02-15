BASENAME=geanygsantnerutils

CFLAGS=`pkg-config --cflags geany` -fPIC -Wall -pedantic -O3 -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"geany-gsantnerutils\"
LDLIBS=`pkg-config --libs geany`

PLUGINDIR_SYSTEM=`pkg-config --variable=libdir geany`/geany
PLUGINDIR_USER="$${HOME}/.config/geany/plugins"

.PHONY: build rebuild clean install install-link all


######################################################################################################

$(BASENAME).so:	$(BASENAME).o $(OBJS)
		gcc -shared -o $@ $(LDLIBS) $^

$(BASENAME).o:	$(BASENAME).c

######################################################################################################

all: clean build install
rebuild: clean build

build:
	@echo "\nBuild..."
#	$(MAKE) po/$(BASENAME)-de.mo
	$(MAKE) $(BASENAME).so

clean:
	@echo "\nClean..."
	rm -f *.o *.so

# Install to system
install-system: build
	@echo "\nInstall..."
	sudo rm -f "$(PLUGINDIR_SYSTEM)/$(BASENAME).so"
	sudo cp "$(BASENAME).so" "$(PLUGINDIR_SYSTEM)"

# Install to home
install: build
	@echo "\nInstall..."
	mkdir -p "$(PLUGINDIR_USER)"
	rm -f "$(PLUGINDIR_USER)/$(BASENAME).so"
	cp "$(BASENAME).so" "$(PLUGINDIR_USER)"

######################################################################################################
# Translation

po/$(BASENAME)-de.mo: po/$(BASENAME)-de.po
	msgfmt --output-file=$@ $<

po/$(BASENAME)-de.po: po/$(BASENAME).pot
	msginit --input=po/$(BASENAME).pot --locale=de --output=po/$(BASENAME)-de.po
	msgmerge --update $@ $<

po/$(BASENAME).pot: $(BASENAME).c
	xgettext --keyword=_ --language=C --from-code=UTF-8 --add-comments --sort-output -o po/$(BASENAME).pot $(BASENAME).c

######################################################################################################
