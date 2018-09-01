Wine/Proton-friendly Warframe launcher
===

This launcher implements enough of Warframe's content distribution to successfully download a working copy of the latest version of the game.

Please note that as of right now, launch strings are hard-coded to be for the Steam version of the game, which might cause issues if you want to use this binary as a replacement launcher for a non-Steam copy of Warframe.

Usage
---

Build the binary yourself or download the pre-compiled version from the [latest release](https://github.com/ananace/wf_proton_launcher/releases).

Put the WF_Proton_Launcher binary into your `Warframe/Tools` folder, you will want to run it from there.  
You can rename the launcher to `Launcher.exe` if you wish, it will not override itself when updating.  
Additional parameters are available, run with `-h` to receive more information about them.

Steam usage for Proton
---

You will want to follow the general usage steps and put the launcher into your Tools folder, rename it to `Launcher.exe`, and add the launch option `-32` to Warframe in Steam.
This works around a Wine issue with 64-bit XAudio.

For a full setup, you'll want to also install the DirectX redist into your Warframe prefix (`~/.steam/steam/steamapps/compatdata/230410/pfx`), and set an override on xaudio2_7.  
You will additionally want to grab the patched wininet DLLs from [GloriousEggroll's repository](https://gitlab.com/GloriousEggroll/warframe-linux/tree/steamplay-proton).

Developing
---

The project should contain all required dependencies, all you need to do is open the solution in Visual Studio 2017 and compile it
