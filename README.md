Wine/Proton-friendly Warframe launcher
===

This launcher implements enough of Warframe's content distribution to successfully download a working copy of the latest version of the game.

Please note that as of right now, launch strings are hard-coded to be for the Steam version of the game, which might cause issues if you want to use this binary as a replacement launcher for a non-Steam copy of Warframe.

Usage
---

Put the WF_Proton_Launcher binary into your `Warframe/Tools` folder, run it from there.  
You can rename the launcher to `Launcher.exe` if you wish, it will not override itself when updating.  
Additional parameters are available, run with `-h` to receive more information about them.

Developing
---

The project should contain all required dependencies, all you need to do is open the solution in Visual Studio 2017 and compile it
