# Spine Viewer

This application allows you to view and record Spine skeletons.
Inspired by [kiletw/SpineViewerWPF](https://github.com/kiletw/SpineViewerWPF). This project improves upon issues of the kiletw's ideas, such as general sluggishness and many many bugs.
Currently works only on Windows.

Supported Spine Editor versions of exported skeletons:
* 3.7.xx
* 3.8.xx
* 4.0.xx
* 4.1.xx

## Installing
There are two ways of installing Spine Viewer: building from source and downloading prebuilt binaries.

To build from source, clone this repository, open the solution in VS2022 and select Build -> Build Solution. 
Note: you may need to copy SFML DLL files to the build output directory in order for the executables to start. You can download them [here](https://www.sfml-dev.org/download/sfml/2.6.0/) (see the "Visual C++ 17 (2022) - 64-bit" variant).

Prebuilt binaries are available in the [Releases](https://github.com/catink123/spine-viewer/releases) section.

## Using
Open up a terminal (Command Prompt, PowerShell, etc.) window in the application's directory. You can right-click (or Shift-right-click) on the empty space in a directory and find "Open a ... window here" option to quickly open a terminal window.

Now, type this command to open Spine Viewer:
```
$ sv-X.X.exe skeleton.atlas skeleton.skel
```
where `X.X` is the version of the Spine Editor used to export your skeleton, `skeleton.atlas` is the path to the Atlas file of your skeleton and `skeleton.skel` is the path to the .skel data-file of your skeleton.

If you have a JSON data-file, specify the `--json` (or `-j`) flag before the data-file path:
```
$ sv-X.X.exe skeleton.atlas --json skeleton.json
```

Press Enter and Spine Viewer should launch if no errors occur. If they do, they will show up in the terminal window you opened.

## Manipulating

To move the skeleton on screen, you can press LMB and drag around in the window. Use the scroll wheel to zoom.

You can change the current animation or skin of the skeleton by opening the "Tools" window.
If you want to manipulate the time of the animation, open the "Animation Controls" window.

## Recording

Use the "Recording Panel" window to start recording the animation frames to PNG images.

In general, you should follow these steps to record an animation:
1. Set the animation and skin you want to record in the "Tools" window.
2. Set the target FPS of the recording in the "Recording Panel" window.
3. Set the Render Scale in the same window if you want to speed up the recording by reducing the saved frames' size.
4. Set the path to an existing directory to save frames to in the Save Path box. If you set an invalid path or the directory doesn't exist, you will get an error in the terminal window when you try to record.
5. Hit the Record button.
