BASENAME=geanygsantnerutils

CFLAGS=`pkg-config --cflags geany` -fPIC -Wall -pedantic -O3
LDLIBS=`pkg-config --libs geany`

PLUGINDIR=`pkg-config --variable=libdir geany`/geany

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
	$(MAKE) $(BASENAME).so

clean:
	@echo "\nClean..."
	rm -f *.o *.so

# Install to system
install: build
	@echo "\nInstall..."
	sudo rm -f $(PLUGINDIR)/$(BASENAME).so
	sudo cp $(BASENAME).so $(PLUGINDIR)

# Install to system with logical link to build filepath, use for development
install-link: build
	@echo "\nInstall (link)..."
	sudo rm -f $(PLUGINDIR)/$(BASENAME).so
	sudo ln -s `pwd`/$(BASENAME).so $(PLUGINDIR)
