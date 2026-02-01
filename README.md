# Roblox ESP (macOS)

A macOS ESP providing **ESP support for any Roblox game**.

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

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

---

## Running the Injector

1. Navigate to the `build` directory.
2. You should see:
    - `injector`
    - `libESPManager.dylib`
3. Run:
   ```bash
   ./injector
   ```

---

## Credits

- Inspired by [notahacker8/RobloxCheats](https://github.com/notahacker8/RobloxCheats)
