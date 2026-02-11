# Roblox ESP (macOS)

A macOS **ESP for almost any Roblox game**.

---

## Preview
![Screenshot 2026-01-31 at 4 52 30 PM](https://github.com/user-attachments/assets/d45040b2-0bf8-4ca1-8cb5-af1d674bdc5a)
![543683142-40eee2bb-6496-420f-a55b-036334253541](https://github.com/user-attachments/assets/8bb5f48d-6b81-4bef-b422-c00d1f1aef79)
![Screenshot 2026-01-31 at 4 57 04 PM](https://github.com/user-attachments/assets/a6b88889-f206-4353-a6bc-1a561902837b)


---

## System Requirements (SIP)

To inject into protected processes on macOS, **System Integrity Protection (SIP) must be disabled**.

### Disable SIP
1. Reboot your Mac into **Recovery Mode**.
2. Open **Terminal** from the Utilities menu.
3. Run: `csrutil disable`
4. Restart your Mac.

---

## Troubleshooting

- **Permission errors**  
  If task attachment fails, [Disable SIP](#disable-sip)

- **Architecture mismatch**  
  Your `.dylib` **must match Roblox’s architecture**.  
  On Apple Silicon Macs, this is typically `arm64`.

---

## Build Instructions

You must clone the project recursively in order to compile it because it uses submodules
```bash 
git clone --recursive https://github.com/TheRouletteBoi/RobloxExternal-macOS.git
```

GUI build requires [vulkan SDK](https://vulkan.lunarg.com/sdk/home)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Headless
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWL_HEADLESS=ON
cmake --build build
```

---

## Running the Injector

1. Navigate to the `build/bin` directory.
2. You should see:
    - `App-Injector-Headless`
    - `libApp-ESPManager.dylib`
3. Run:
   ```bash
   ./App-Injector-Headless
   ```

---

## Credits

- Inspired by [notahacker8/RobloxCheats](https://github.com/notahacker8/RobloxCheats)
- Thanks to [@c7a2d9e](https://github.com/c7a2d9e) for probing rtti on macos
- nopjo roblox dumper source [roblox-dumper](https://github.com/nopjo/roblox-dumper)
