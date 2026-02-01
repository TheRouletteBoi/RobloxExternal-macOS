# Roblox ESP (macOS)

A macOS ESP providing **ESP support for any Roblox game**.

---

## Preview
![Screenshot 2026-01-31 at 4 52 30 PM](https://github.com/user-attachments/assets/d45040b2-0bf8-4ca1-8cb5-af1d674bdc5a)
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
