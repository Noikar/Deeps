# Deeps for Ashita v4
Credit to kjLotus for the original version of Deeps that was the basis for this update.

Special thanks to Thorny for help with refactoring and general advice and tips on Ashita development!

Also thanks to ShiyoKozuki for helping test and helping me figure out some packet stuff.

Forked from https://git.ashitaxi.com/Plugins/Deeps

This fork continues from [relliko/Deeps](https://github.com/relliko/Deeps), which is no longer being maintained.

## Installation
- Download and extract the ```plugins``` and ```resources``` directories from the [latest release zip](https://github.com/Noikar/Deeps/releases/latest) directly into your base Ashita v4 directory.
- Type /load deeps in game

Note: The only necessarily required file is ```plugins/Deeps.dll```; if you don't like the bar texture you can leave out the resources folder and just have flat colored bars and the plugin will still work, although some jobs may have text that is difficult to read.


## Usage
You can type /dps or /deeps to show the available commands. 

Left clicking on a bar will show additional details about the damage dealt, right click to go back.

Shift clicking the background will allow you to reposition the window.


## New features
- Pet damage included in player's damage contribution
- Static colors for job bars
- Damage from outside of party or alliance can now be filtered out
- Overall hit rating displayed alongside damage done

## Known issues
- Settings may not save under certain conditions. To remedy this, change your settings and then `/unload deeps`, it should then save.
- Additional effects contribute towards overall accuracy
- The way crit percentage is displayed doesn't account for misses. Thus, your crit rate is going to look lower than it actually is.
- Spikes damage, counters, reprisal procs do not count currently.
- High jump displays as Avalanche and Jump displays as Gale axe
- Report only shows top 4
- ~~Missing the first swing of attack rounds may not include the rest of the rounds damage~~ I haven't seen any real evidence of this. If you have it, show me.

## Building
Requires Visual Studio (or the Build Tools) with the C++ desktop workload. The build targets `Release|Win32` — Ashita v4 is a 32-bit host, so x86 is the only configuration that produces a usable plugin.

Point the build at your Ashita v4 SDK (the folder containing `Ashita.h`, normally `<Ashita>\plugins\sdk`) in either of these ways:

```
:: option 1 - environment variable
set ASHITA_SDK_PATH=E:\Games\HorizonXI\Game\plugins\sdk
msbuild Deeps\Deeps.vcxproj /p:Configuration=Release /p:Platform=Win32

:: option 2 - pass it directly
msbuild Deeps\Deeps.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:AshitaSDK="E:\Games\HorizonXI\Game\plugins\sdk"
```

If neither is set, the build falls back to `..\..\sdk`, which resolves correctly when the repo is checked out inside your Ashita `plugins` directory.

The project pins the `v143` toolset (VS 2022). On newer Visual Studio installs, add `/p:PlatformToolset=v145` (or whichever version you have).

The resulting `Deeps.dll` is written to `release\plugins\`.

## Patch Notes

### v1.07
- Added the `expDestroyPlugin` export required by Ashita interface 4.30, which was causing the plugin to fail to load. Thanks to @troyBORG for the original fix ([relliko/Deeps#12](https://github.com/relliko/Deeps/pull/12)).
- Recompiled against the Ashita 4.30 SDK.
- The SDK include path is no longer hardcoded to one machine, so the project builds from a clean checkout.

### v1.06
- Added a configuration setting (`/dps sc`) to disable skillchains counting towards a player's damage contribution.
- Hopefully fixed pet damage ending up associated with the wrong owner after resummoning.
- Fixed colors being random when someone is /anon, now it is consistently blue.
- Spells no longer affect overall hit rate

### v1.05
- Fixed SMN blood pacts not being included in pet damage
- Added /dps tvmode to scale the size up by 50% so it's easier to look at on big screens.

### v1.04
- Fix for crash while clicking bars

### v1.03
- Stability fixes
- Improved visibility of DRK bars

### v1.02
- Pet damage now counts towards a player's total damage contribution. It should not affect the displayed overall hit rate
- Added a setting to toggle static job colors, typing /dps jobcolors will bring back randomized coloring for jobs
- Added a setting to display data from non-party members, toggle by typing /dps partyonly

## TODO (No guarantees)
- Config to exclude skillchain damage or display skillchains as their own category
- Log saving to disk
- Setting to reset on every kill
