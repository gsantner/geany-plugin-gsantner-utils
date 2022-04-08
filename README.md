## Geany Plugin: gsantner utils
A [Geany](http://geany.org) plugin with various utilies and improvements to general Geany usage.


### Features
![screenrecord](https://user-images.githubusercontent.com/6735650/64099809-bab94c00-cd59-11e9-9ba2-eb74c3dd912f.gif)

* json_reformat (Tools menu option)
  * Reformat & reindent the JSON content of the file currently open in editor
* Favourites (File menu option, Toolbar option)
  * You very often open the same files and keep browsing for it? Than that's what you need!
  * Adds a easy accessible option for global favourites
  * Independent to projects, recent files & currently open files
  * Configuration in `geany.conf`, `geanygsantnerutils` group, `favourites` key
    * Semicolon separated list: `TITLE;FILEPATH`. 
    * TITLE: `---` adds a separator (no FILEPATH required). `>>` is replaced by `»`.
    * FILEPATH: `$HOME` is replaced by current user home directory.
* Improved sidebar (Tab symbols, files, projects, ...)
  * Rotate tab text to vertical
  * Add a arrow at begin of each tab for easier idenification
  * Use noticeable different color for selected tab
  * Bigger font size and differnt monospace font
* Unclutter default Geany UI
  * Hide some buttons and menu options, whose funconality can be accessed by default on 3-4 places (e.g. keybinding, toolbar, project menu, file menu, file list, ...)
  * Hide close (one/all/other files), save (one/all)

#### Configuration example
```
 # ~/.config/geany/geany.conf

[geanygsantnerutils]
favourites=myScripts >> shellscript.sh;/mnt/usb/myScripts/shellscript.sh;---;geany.conf;$HOME/.config/geany/geany.conf
```


### Installation
* Install Geany, GNU C compiler and build-essentials.
* Run `make install`
* Close all instance of Geany and restart
* Open plugin list and enable geanygsantnerutils plugin

### Development
This is the easiest option to develop on this plugin, go to the repo and execute command below.
It allows you to edit the plugin in Geany while seeing changes of last build. Quit Geany and it's automatically restarted with new plugin development version.
```
while [ true ] ; do make install && geany -v && sleep 0.2; done
```

#### Resources
* geany reference - https://www.geany.org/manual/reference/
* gtk reference - https://developer.gnome.org/gtk3/stable/
* glib reference - https://developer.gnome.org/glib/2.60/
* geany source code - https://github.com/geany/geany
* geany glade - https://github.com/geany/geany/blob/master/data/geany.glade
* geany manual - https://www.geany.org/manual/dev/index.html

