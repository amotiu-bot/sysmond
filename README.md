# sysmond — System Monitoring Daemon for FRDM-IMX93

A lightweight system monitoring daemon for the NXP FRDM-IMX93 development board.
It monitors CPU usage and system temperature, logs via syslog, and provides
interactive LED feedback using the onboard K2/K3 pushbuttons and RGB LED1.

---

## Features

- Periodic logging of CPU usage and temperature via syslog
- Configurable logging frequency via `/etc/sysmond.conf`
- K2 button: LED1 shows CPU temperature (BLUE / WHITE / RED)
- K3 button: LED1 shows CPU usage (GREEN / YELLOW / RED)
- Managed by systemd, auto-starts on boot

---

## Hardware Mapping

| Component | Interface        | Details                        |
|-----------|-----------------|--------------------------------|
| LED1 Red  | PWM (pwmchip4 pwm2) | GPIO_IO13 → TPM4_CH2       |
| LED1 Blue | PWM (pwmchip0 pwm2) | GPIO_IO12 → TPM3_CH2       |
| LED1 Green| PWM (pwmchip0 pwm0) | GPIO_IO04 → TPM3_CH0       |
| K2 Button | gpiochip4 line 5    | User Button 1              |
| K3 Button | gpiochip4 line 6    | User Button 2              |

---

## LED Color Codes

### K2 — CPU Temperature
| Color  | Temperature       |
|--------|-------------------|
| BLUE   | Below 50°C        |
| WHITE  | 50°C – 70°C       |
| RED    | Above 70°C        |

### K3 — CPU Usage
| Color  | CPU Usage         |
|--------|-------------------|
| GREEN  | Below 60%         |
| YELLOW | 60% – 80%         |
| RED    | Above 80%         |

---

## Configuration

Edit `/etc/sysmond.conf` to change the logging frequency:

```
# Logging interval in seconds
log_interval=5
```

Restart the daemon after changes:
```bash
systemctl restart sysmond
```

---

## Viewing Logs

```bash
# View live logs
journalctl -f -u sysmond

# View all sysmond logs
journalctl -u sysmond

# View via syslog
tail -f /var/log/messages
```

---

## Building with Yocto

### 1. Add the recipe to your layer

Copy `bitbake/sysmond_git.bb` to your Yocto layer:
```bash
cp bitbake/sysmond_git.bb <your-layer>/recipes-sysmond/sysmond/sysmond_git.bb
```

Update the `SRC_URI` and `LIC_FILES_CHKSUM` in the recipe with your actual GitHub URL and LICENSE md5sum:
```bash
md5sum LICENSE
```

### 2. Add to your image

In `conf/local.conf`:
```
IMAGE_INSTALL:append = " sysmond"
```

### 3. Build
```bash
bitbake imx-image-core
```

---

## Building Manually (for development)

```bash
# Native build
make

# Cross-compile for ARM64
make CROSS_COMPILE=aarch64-linux-gnu-

# Install
make install DESTDIR=/path/to/rootfs
```

---

## Managing the Service

```bash
# Check status
systemctl status sysmond

# Start/stop
systemctl start sysmond
systemctl stop sysmond

# Enable/disable auto-start
systemctl enable sysmond
systemctl disable sysmond
```

---

## Dependencies

- `libgpiod` — GPIO access for K2/K3 buttons
- Linux kernel with TPM3/TPM4 PWM drivers enabled (default in FRDM-IMX93 BSP)

---

## License

MIT License — see [LICENSE](LICENSE) for details.
