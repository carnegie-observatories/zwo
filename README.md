- Releases: https://github.com/carnegie-observatories/zwo/releases
- Old documentation webpage: https://instrumentation.obs.carnegiescience.edu/Software/ZWO/

# Release Installation Instructions

1. Download the latest release from the [Releases page](https://github.com/carnegie-observatories/zwo/releases).
2. Run the executable `zwoserver-<version>-<hash>.run` as root.

    sudo ./zwoserver-<version>-<hash>.run

### Building zwo server on Docker


    docker build -t ghcr.io/carnegie-observatories/zwo:latest .    
    docker rm -f temp-container
    docker run --name temp-container ghcr.io/carnegie-observatories/zwo:latest
    docker cp temp-container:/app/zwoserver.run .
    docker stop $(docker ps -q)

### "Testing" with Docker

    docker run -it zwo
    LD_LIBRARY_PATH=/usr/local/lib/ /app/zwo/src/server/zwoserver &
    telnet localhost 52311asdasd

###  GCAM on Docker

To build and run the GCAM container, use the following commands:

    docker-compose build gcam
    docker-compose up gcam

To access the GCAM container over VNC, open a browser and navigate to:

    http://localhost:3000/


### ZwoServer Command Protocol

**Connect to the _zwoserver_ via `nc` (or netcat or similar):**

```

nc rpi 52311

```

**Protocol:**
- All commands have to be terminated by a `[LF]` character (ASCII: 0x0a)
- All responses will be terminated by a `[LF]` (except binary image data)

---

**Command:** `version`  
Returns the version string, a cookie, and the startup time of the server.

---

**Command:** `open`  
Opens the USB connection to the camera – does nothing if already connected.  
Returns the chip geometry, cooler and color availability, examples:  
- `"1936 1096 0 1"` : no cooler, color (ASI290MC)  
- `"4656 3520 1 0"` : has cooler, monochrome (ASI1600MM)

---

**Command:** `setup [ x y w h b p ]`  
Returns the current setup or changes the readout geometry (example: `0 0 4656 3520 1 8`)

- `[x y w h]` : window geometry  
- `[b]` : binning {1,2,4}  
- `[p]` : bits-per-pixel {8,16,24}  

The values for `x y w h b p` are stored to disk.

**Command:** `setup default`  
Loads the values stored via the last `setup x y w h b p`.

**Command:** `setup image [ b ]`  
Same as `setup 0 0 4656 3520 1 16` — the optional `b` parameter overwrites the binning.

**Command:** `setup video [ b ]`  
Same as `setup 0 0 4656 3520 1 8` — the optional `b` parameter overwrites the binning.

---

**Command:** `exptime [ # ]`  
Sets the exposure time in seconds `{0.0001 .. 30.0}`

**Command:** `gain [ # ]`  
Set the CCD gain `(0..600)`

**Command:** `offset [ # ]`  
Set the CCD offset `(0..100)`

---

**Command:** `expose`  
Starts an exposure. A return value of `"0"` signifies success.  
Any error will start with `"-E"`.

---

**Command:** `status`  
Returns the current server status:
- `"closed"` : the connection to the USB camera is closed.
- `"idle"` : connected to the USB camera; no exposure or video stream is active.
- `"exposing #"` : exposure is running (for `#` seconds).
- `"streaming"` : streaming video from the camera.

---

**Command:** `data [ # ]`  
Read out the image data (exposure or video) and send it as binary data.  
The optional parameter limits the number of binary data bytes being sent (`0` allows omitting sending binary data for testing).  
Returns the number of bytes in the image, followed by the binary data.

---

**Command:** `start`  
Start video streaming from the camera.  
The images may be transmitted via the `data` command.

**Command:** `stop`  
Stop video streaming.

---

**Command:** `write [ # ]`  
Writes the current image (exposure or video) to disk as `$HOME/zwo0000.fits`.  
The `0000` number is incremented after each `write` command.  
`#` sets the file number.

---

**Command:** `close`  
Closes the connection to the USB camera.  
**Note:** `close` is implicit when the network connection is terminated.

---

**Command:** `tempcon [ # ]`  
Set the temperature control setpoint; `"off"` turns off the temperature control.  
Without parameter it returns the temperature and cooler percentage.  
Example: `0.0 25`

---

**Command:** `fancon [ # ]`  
Set the fan control; `off` turns off the fan. `on` turns it on.
Without parameter it returns the current fan status as a integer 0 (off) or 1 (on).
Example: `1`

---

**Command:** `filters`  
Returns the number of positions on the filter wheel.  
If no filter wheel is connected the return is `"0"`.  
**Note:** Must be called before the wheel can be moved.

**Command:** `filter [ # ]`  
Move the filter wheel to position `#` (the index is `0`-based).  
Return: `"0"` (move accepted)  
Without parameter it returns the current filter wheel position or `"-1"` for "moving".
