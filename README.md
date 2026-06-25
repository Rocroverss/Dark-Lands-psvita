
# Dark Lands — PSVita Port

**Dark Lands** is a 2D action-platformer in which players take on the role of an Ancient Greek hero. With nothing but their reflexes and a faithful sword, they must survive a dark world filled with shadows, deadly obstacles, and relentless foes.

Become a Warrior Hero in this epic combat runner game. Clash with monsters, crush bosses, and survive traps to run as long as possible!

This is a wrapper/port of **Dark Lands** for the *PS Vita*.

The port works by loading the official Android ARMv7 executable in memory, resolving its imports with native functions and patching it in order to properly run.
By doing so, it's basically as if we emulate a minimalist Android environment in which we run natively the executable as it is.

---

## Changelog

### v1.0

- Initial Release.

---

## 🌐 Official Game Download

- [amazon](https://www.amazon.com/Bulkypix-DarkLands/dp/B00L1DZTDS)  

---

## Setup Instructions (For End Users)

- Install [kubridge](https://github.com/TheOfficialFloW/kubridge/releases/) and [FdFix](https://github.com/TheOfficialFloW/FdFix/releases/) by copying `kubridge.skprx` and `fd_fix.skprx` to your taiHEN plugins folder (usually `ux0:tai`) and adding two entries to your `config.txt` under `*KERNEL`:
  
```
  *KERNEL
  ux0:tai/kubridge.skprx
  ux0:tai/fd_fix.skprx
```

**Note** Don't install fd_fix.skprx if you're using rePatch plugin

- **Optional**: Install [PSVshell](https://github.com/Electry/PSVshell/releases) to overclock your device to 500Mhz.
- Install `libshacccg.suprx`, if you don't have it already, by following [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
- Install the vpk from Release tab.
- Obtain your copy of *KatzenKlein Redux* legally for Android in form of an `.apk` file.
- Open the apk with your zip explorer and extract the files `libmain.so` from the `lib/armeabi-v7a` folder to `ux0:data/dla`. 
- Put the `assets` folder (the one that contains maps, music, shader... not the parent) inside `ux0:data/dla` . 

---

## Build Instructions (For Developers)

In order to build the loader, you'll need a [vitasdk](https://github.com/vitasdk) build fully compiled with softfp usage.  
You can find a precompiled version here: https://github.com/vitasdk/buildscripts/actions/runs/1102643776.  
Additionally, you'll need these libraries to be compiled as well with `-mfloat-abi=softfp` added to their CFLAGS:

- [SDL2_vitagl](https://github.com/Northfear/SDL/tree/vitagl)

- [libmathneon](https://github.com/Rinnegatamante/math-neon)

  - ```bash
    make install
    ```

- [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK)

  - ```bash
    make install
    ```

- [kubridge](https://github.com/TheOfficialFloW/kubridge)

  - ```bash
    mkdir build && cd build
    cmake .. && make install
    ```

- [vitaGL](https://github.com/Rinnegatamante/vitaGL)

  - ````bash
    make NO_DEBUG=1 SOFTFP_ABI=1 HAVE_GLSL_SUPPORT=1 USE_SCRATCH_MEMORY=1 CIRCULAR_VERTEX_POOL=2 install -j11
    ````

After all these requirements are met, you can compile the loader with the following commands:

Build it like this for release:
```bash
cmake -S . -B build -DDEBUG=0 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Build it like this for debugging/log output:
```bash
cmake -S . -B build -DDEBUG=1
cmake --build build
```

---

## 📸 Screenshots

![Screenshot 1](img/IMG4.png) 
![Screenshot 2](img/IMG5.png) 
![Screenshot 3](img/IMG6.png) 
![Screenshot 4](img/IMG7.png)  
![Screenshot 5](img/IMG8.png)
![Screenshot 6](img/IMG9.png)

---

## Credits

- TheFloW for the original .so loader.
- Rinnegatamante for the help in other ports and the marvelous [vitaGL](https://github.com/Rinnegatamante/vitaGL/tree/master) that made this port possible.
  
---

## Credits

- BonQ for help in the porting process and suggesting me this game.
- [Wolff](https://github.com/WolffsRoom/) for reminding me that this project was a candidate.
