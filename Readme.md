# *Turbo*

A minimalistic frontend for the *gdb* debugger.

Please note that this is a work in progress, and has imperfections. The best way to improve this program is by addressing issues reported from people who actually use it - bug reports, what needs improving, and what would be nice to add to this program.

Thanks to all who decide to give this program a try! If you find this program useful, please let me know of any problems you discover, and what you would like to be changed or added - this is important and gives me motivation to continue working on this program.

### Motivation

Why another *gdb* frontend? There are already so many good *gdb* frontends, so why another one?

This frontend attempts to be a minimalistic, very thin wrapper around the *gdb* debugger, with a focus on remote debugging. Native debugging has its own challenges and peculiarities, and is currently out of scope for this frontend. Also, attempts are made to seamlessly interface with the powerful [blackmagic probe](https://github.com/blacksphere/blackmagic "Blackmagic probe home page"). At the moment, the frontend only supports connecting to the **blackmagic**  probe hardware directly. However, another, potentially much more powerful possibility exists - connecting to the **blackmagic** *hosted* variant, which supports more hardware probes, and has enhancements not present in the *blackmagic* probe firmware. Such support is intended to be added in the future.

For reasons that will be made clear later, this frontend works best when debugging programs written in the **C** programming language. Of course, debugging programs written in any language supported by *gdb* is available, but the best debugging experience is with programs written in the **C** programming language.

### Building the software
All of the prerequisite libraries (currently, this is only the [ELFIO library](https://github.com/serge1/ELFIO "ELFIO home page")) are provided in the project's github repository. The software is released under an MIT license. The Qt framework is being used for driving the graphical user interface. The Qt framework is freely available for download for non-commercial use. If you are downloading the Qt framework package for the first time, you may find it unusually hard to locate the list of offline installer packages, so here is a [direct link to the Qt offline installers](https://www.qt.io/offline-installers "Offline Qt Downloads"). Installing the Qt package, however, requires performing a free registration on the Qt site.

An alternative is to use an installer package for your Linux system, that should work as well. I personally prefer to use the official Qt installers. If you decide to download Qt from the official site, remember to make the downloaded file executable (`chmod +x installer-package`) before running it.

There are two ways to build the software - use the `qmake` and `make` utilities from the command line, or perform building from the **Qt Creator** IDE (which, eventually, will run `qmake` and `make` to perform the build). I personally prefer using the IDE, because it is very convenient for making changes to the user interface through its interactive user interface editor.

To build the software from the **Qt Creator** IDE, start the IDE, open the project file - `turbo.pro`, and build the project. That is all.

To build from the command line:
- for Windows systems, open a shell (command window) that is preconfigured for executing Qt builds. You will find such a preconfigured command shell as part of the Qt installation on Windows. Create an empty directory that will contain the built files, and perform the building there:
```
# Get the sources from github
git clone https://github.com/stoyan-shopov/turbo
# Create a directory to contain the build files
mkdir turbo-build
cd turbo-build
qmake ../turbo/turbo.pro -spec win32-g++ "CONFIG+=debug"
mingw32-make
# The built executable is 'debug/turbo.exe'
```
- the procedure is similar for Linux systems. However, there are some prerequisites that you may need - these are an `ncurses` library that may be needed in order to be able to run the `arm-none-eabi-gdb` debugger, and the `mesa-libGL` development library that may be needed to successfully link the software. How exactly to install these packages depends on the Linux distribution used. Try first building the software without installing these libraries, and get back to this step if it turns out you need them. On a Centos 8 system that I am using, installing these libraries can be done with these commands:
```
sudo dnf install ncurses-compat-libs.x86_64
sudo install mesa-libGL-devel.x86_64
```
Then, the frontent can be built (make sure to have the `qmake` program available in the path):
```
# Get the sources from github
git clone https://github.com/stoyan-shopov/turbo
# Create a directory to contain the build files
mkdir turbo-build
cd turbo-build
qmake ../turbo/turbo.pro -spec linux-g++ CONFIG+=debug
make
# The built executable is named 'turbo'
```
**Important**: you may need to add your user to the **dialout** group on Linux systems, in order to be able to open the blackmagic probes as a non-root user, for example:
```
sudo usermod -a -G dialout username
```

### User interface

Let us have a look at what is seen when the freshly built frontend is run:

![Gdb has not been set][gdb-not-set-message]

Because the frontend is being run for the first time, it needs some configuration to be specified. As a minimum, it is absolutely essential to specify the **gdb** executable that the frontend will use. This is how the configuration dialog looks like:

![Empty configuration dialog][empty-configuration-dialog]

##### NOTE: It s **HIGHLY** recommended that you use a recent **gdb** version. Versions lower than 10 are poorly supported. The reason for this is that **gdb** version 10 and above have the ability to report program symbol information to the frontend, which is used for rapid program navigation by the frontend.

The pale grey text is a hint about what you need to specify in this dialog. It is absolutely essential to specify a valid **gdb** executable, the frontend will not work without one. If you intend to invoke an external editor from the frontend, for editing files, you can configure your prefered editor here as well.

##### NOTE: the **Turbo** frontend is NOT an IDE. It can NOT be used for editing files. For that, you need an external editor.

Once the **gdb** program has been configured and successfully started, you will need to specify an executable file to debug:

![Choosing a file for debugging][file-open-dialog-empty]

If you specify a valid executable, and **gdb** successfully opens it, the frontend will be ready for performing debugging operations.

##### NOTE: The file opening dialog will keep a list of the most recently files that were loaded in the debugger. This way, switching between several projects that you are working on, is made very fast.

Once the file has been successfully loaded in the debugger, you should see the primary frontend window:

![Frontend view on startup][frontend-view-basic]

You can see that the frontend is organized as a central text view area, around which there are some views and controls. On startup, there is some brief help displayed in this area. Source code is also displayed in this area. Around the central area, there are some docking windows for displaying various information. In the picture shown, these are the **Source files** view, which shows all source code files that are part of the program being debugged. This tree-like view can be used for rapid navigation between files, and functions within files. The frontend supports placing bookmarks (by pressing the `<F2>` key), and in the picture shown the **Bookmarks** view is also visible. Visible is also the **Object locator** view - it has a pale blue background so that it is easily to visually identify. This is a powerful view, in which you can start typing a string, and you will get a list of all known program objects that match the string typed - these objects can be source code file names, functions, variables, data types. This view is used for rapidly navigating to a place of interest in the program.

##### NOTE: The lists of source code file names, function and variables names, and data type names are extracted by **gdb** from the debug information of the program being debugged, and are then reported to the frontend. This information is the only source of knowledge for the frontend to locate symbols in the program. If a symbol that you believe must be present in the program, is nonetheless not reported in the **Object locator** view, then this means that this symbol was not reported to the frontend by gdb. A very common example of this is a function that was inlined by the compiler. **Gdb** may choose to not report all such functions, even though it often could report them, based on the debug information contained in the program.

At the bottom of the picture, the **Search** view is visible. It contains the result of searching all known files for a given string.


##### NOTE: to search for a string in the program - either type it in the **Search files for text** field in the **Search** view, and press `<enter>`, or click with the mouse on a string in the source code while holding the `<SHIFT>` key. To attempt to directly navigate to a symbol definition on the program, click with the mouse on a string in the source code while holding the `<CONTROL>` key.

##### NOTE: Searching in files is currently not too useful. The reason for this is that there is no **gdb** command to report the list of ***all*** source code files that were processed by the compiler in order to build the final executable. This includes many of the header files that were part of the compilation. This information, in fact, does exist in the debug information contained in the executable, but there is no **gdb** command to request this information. It is intended to address this issue in the future. One option would be to access the **DWARF** debug information directly, or, perhaps much simpler, to just run an external utility (e.g., `readelf`, `objdump`) to dump the debug information, and parse the output, in order to retrieve the complete list of source code files that took part in the compilation.

Other than the data view windows, there are a number of other controls - mainly buttons and checkboxes. As there is no currently target attached, many of the buttons are disabled. It should be clear what most of the buttons do. Here is a brief description of the controls visible in the picture:

![Gdb controls][gdb-controls]

 - `Halt` button - halt a running target
 - `Verify flash` button - compare the contents of the target memory against the contents of the loaded executable file for debugging
 - `Load program to target` button - load the contents of the executable being debugged into the target memory
 - `Reset and run` button - restart the program from the beginning
 - `Continue` button - resume a halted tarrget
 - `Step over` button - execute a single step (source-code-wise), stepping over function calls
 - `Step into` button - execute a single step (source-code-wise), stepping into function calls
 - `Disconnect gdb` button - disconnect gdb from a remote gdb server. Maybe this button will be removed - activating it just sends a `disconnect` command to gdb.
 - `Scan and attach` button - when a connection to a **blackmagic** probe is established, activating this button will send a `monitor swdp_scan` request to the **blackmagic**, the response will be parsed, and if a target is available, the first target will be attached. Basically, this button saves you from manually typing `monitor swdp_scan`, `attach 1` commands directly to gdb
 - `Start gdb` button - this button should be rarely needed. It can be used in order to start the **gdb** debugger, in case it exited for some reason, e.g., you typed `quit` in **gdb**, or **gdb** crashed

The other controls need a bit more explanation. In the toolbar, at the top of the window, there are these controls:

![Main toolbar controls][controls-main-toolbar]

 - `Show window` button - activating this button displays a menu which looks like this:

![Show window popup menu][show-window-menu]

Here, you can conveniently see which display views are available, whether they are currently shown, and you can toggle between shown and not shown depending on your preferences
 - `Locate window` button - activating this button displays a menu which looks like this:

![Locate window popup menu][locate-window-menu]

This menu can be used for quickly locating a view window - by selecting the view of interest in this menu, this view will be made visible (in case it was not visible), and it will be visually identified by briefly flashing it. This menu can be very useful, so there is a shortcut to showing it - tab the `<CTRL>` key twice.
 - `Select layout` - combo box - select between a couple of preconfigured layouts for the frontend user interface. Currently, this control is not very useful, because the predefined layouts need to be constructed better
 - `Settings` button - open the settings dialog
 - `Help` button - display brief help. This needs to be improved
 - `Go back` button - navigate to the previous source code location (if any) in the navigation stack, when browsing the program source code. The keyboard shortcut is `<ALT> + <LEFT ARROW>`
 - `Go forward` button - navigate to the next source code location (if any) in the navigation stack, when browsing the program source code. The keyboard shortcut is `<ALT> + <RIGHT ARROW>`
 - `RESTART` button - restart the frontend. Restarting the frontend is described in a section of its own.
 - `Connect to blackmagic` button. Activating this button will attempt to automatically detect, and connect to an available **blackmagic** probe. In case only one **blackmagic** probe is found, an attempt to connect to it is made. In case more than one blackmagic probe is found (common, when debugging the **blackmagic** probe with another instance of itself), a dialog to select the probe is shown:

![Select blackmagic probe dialog][select-blackmagic-probe]

 - `ACTION` tool button. Activating this button displays this menu:

![Actions menu][actions-popup-menu]

The purpose of this menu is to provide a compact list of various available actions.

There also is this set of controls in the central area:

![Main controls][controls-main]

 - `Gdb stderr` checkbox - this has proven not to be useful, and may be removed
 - `Disassembly` checkbox - toggle the `Disassembly` view (shown in screenshots below)
 - `Show gdb consoles` checkbox - toggle the **gdb** console (shown in screenshots below). In this console, the **gdb** are displayed responses to commands that the user (or the frontend) issue to **gdb**
 - `Gdb command` line edit - this control is used for directly sending commands to **gdb** by the user. To issue a command to **gdb**, type the command in this field, and press `<ENTER>`. The **gdb** response will be displayed in a **gdb** console view (shown in screenshots below)

This covers the basic functionality of the ***Turbo*** frontend.

### Complete frontend user interface

This is a minimal view of the frontend, with all views disabled. In this view, the user communicates directly with **gdb**, by sending commands to **gdb**, and observing **gdb** responses. The current source code location is tracked by the frontend:

![Minimal view][minimal-view]

Here is a brief review of the complete frontend user interface. Please note, that the pictures below show all available views in the frontend, but, typically, when debugging, only a subset of these views will be needed, so the view is normally much less complicated and tidy, and not so confusing:

![Complete view][complete-view]

Here is an even more confusing picture, showing detached views as well:

![Complete view, with separate windows][complete-view-whole-screen]

These are extreme examples, hopefully, such confusion will seldom be necessary in normal use of this program.

One of the major goals of the ***Turbo*** frontend is to allow you to compose your debugging session the way that is most convenient for you. Think of this frontend as a tailor, making a suit to fit your particular preferences. These views are shown in the picures above:

 - Static data objects list (this is a misnomer, what is meant is `objects with static storage duration`, in **C** language terminology)
 - Subprograms list
 - Registers list
 - Object locator view
 - Stack backtrace view
 - Breakpoints list
 - Data objects view, requested by the user
 - Local variables (again a misnomer, this is the list of `objects with automatic storage duration` in **C** language terminology, i.e., stack variables)
 - Data types list
 - System View Description (SVD), as specified by ARM, view
 - Disassembly view
 - Gdb console view
 - Source view
 - Bookmarks list
 - Target memory dump view
 - Scratchpad. The `scratchpad` is a primitive text-holding container, where you can put notes. The scratchpad contents survive a restart of the frontend

The Qt framework used in the frontend provides powerful facilities for constructing user interfaces. It can be seen in the pictures that the particular views can be positioned in an arbitrary manner around the central source code view - they can be docked around the central view, stacked upon one another, detached from the main frontend window itself.

A view can be closed, if not needed at a particular moment. A view can be recalled, most efficiently, by tapping the `<CTRL>` key twice, or by triggering either one of the `Show window`, or `Locate window` buttons. Triggering the `Locate window` button does the same as tapping the `<CTRL>` key twice.

    DOCUMENT NOT FINISHED

[gdb-not-set-message]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/gdb-executable-not-set.png "Gdb not set message"
[empty-configuration-dialog]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/settings-dialog-not-filled.png "Empty configuration dialog"
[file-open-dialog-empty]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/file-open-dialog-empty.png "File open dialog"
[frontend-view-basic]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/basic-window-layout-on-startup.png "Frontend view on startup"
[show-window-menu]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/show-window-popup-menu.png "Show windows popup menu"
[locate-window-menu]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/locate-window-popup-menu.png "Locate windows popup menu"
[select-blackmagic-probe]: https://raw.githubusercontent.com/stoyan-shopov/turbo/master/documentation/images/probe-selection-dialog.png
[gdb-controls]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/controls-gdb-control.png
[controls-main-toolbar]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/controls-main-toolbar.png
[actions-popup-menu]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/action-popup-menu.png
[controls-main]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/controls-main.png
[complete-view]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/screenshot-large.png
[complete-view-whole-screen]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/screenshot-large-whole-screen.png
[minimal-view]: https://raw.githubusercontent.com/stoyan-shopov/turbo/devel/documentation/images/minimal-view.png
