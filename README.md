# FastDepthTap

FastDepthTap is a lightweight Unreal Engine 4.27 runtime plugin for fast, on-demand depth capture.
It is designed to work with projects that use **AirSim** as the simulation environment or camera system foundation.

The plugin listens for local UDP commands, triggers a `SceneCaptureComponent2D`, reads the render target data back to CPU memory, and exports depth maps as `.npy` or `.pfm` files. It also supports optional grayscale PNG visualization and camera pose logging.

## Overview

FastDepthTap is mainly used for synthetic data generation, depth map collection, robotics simulation, computer vision experiments, and depth estimation dataset preparation.

Instead of manually capturing frames inside Unreal Engine, this plugin allows external programs, such as Python scripts, to send UDP commands and trigger depth capture automatically.

## Prerequisites

Before using this plugin, make sure your Unreal Engine project already has:

* Unreal Engine 4.27
* AirSim plugin installed and configured
* A valid Unreal project that can run in PIE or packaged Game mode
* At least one `SceneCaptureComponent2D`
* A valid `TextureRenderTarget2D` assigned to the capture component

## Features

* Runtime plugin for Unreal Engine 4.27
* Works with AirSim-based Unreal projects
* Local UDP command control
* On-demand depth capture
* SceneCapture2D render target readback
* Export depth maps as:

  * `.npy`
  * `.pfm`
* Optional grayscale PNG visualization
* Optional camera-follow mode
* Capture component selection by name
* Render target selection by name
* Player camera pose logging
* Capture camera pose logging
* Dedicated log file output

## Project Structure

```text
Plugins/FastDepthTap/
├── FastDepthTap.uplugin
└── Source/
    └── FastDepthTap/
        ├── FastDepthTap.Build.cs
        └── Private/
            └── FastDepthTapModule.cpp
```

## Installation

1. Install and configure AirSim in your Unreal Engine 4.27 project.

2. Copy the plugin folder into your Unreal project:

```text
YourProject/
└── Plugins/
    └── FastDepthTap/
```

3. Make sure the final structure looks like this:

```text
YourProject/
├── Plugins/
│   ├── AirSim/
│   └── FastDepthTap/
└── YourProject.uproject
```

4. Open the project in Unreal Engine 4.27.

5. Enable the plugin if it is not enabled automatically:

```text
Edit → Plugins → Rendering → Fast Depth Tap
```

6. Restart Unreal Engine.

7. Rebuild the project if Unreal asks you to compile the plugin.

## UDP Control

FastDepthTap listens on:

```text
127.0.0.1:7779
```

The plugin only responds to commands that start with:

```text
shoot
```

## Basic Command Example

```text
shoot dir=E:/dump seq=1 fmt=npy base=depth_ png=1 follow=1
```

This command captures one depth frame and saves it as:

```text
E:/dump/depth_0001.npy
E:/dump/depth_0001_vis.png
```

## Command Parameters

| Parameter | Description                                      | Example                   |
| --------- | ------------------------------------------------ | ------------------------- |
| `dir`     | Output directory                                 | `dir=E:/dump`             |
| `seq`     | Sequence number                                  | `seq=1`                   |
| `fmt`     | Output format, supports `npy` or `pfm`           | `fmt=npy`                 |
| `base`    | Output filename prefix                           | `base=depth_`             |
| `log`     | Custom log file path                             | `log=E:/dump/fdt.log`     |
| `cap`     | Select SceneCaptureComponent2D by name substring | `cap=DepthCapture`        |
| `rt`      | Select render target by exact name               | `rt=RT_DepthMeters_1080p` |
| `follow`  | Make capture camera follow player camera         | `follow=1`                |
| `png`     | Export grayscale PNG visualization               | `png=1`                   |
| `vmin`    | Minimum depth value for PNG visualization        | `vmin=0`                  |
| `vmax`    | Maximum depth value for PNG visualization        | `vmax=5000`               |
| `gamma`   | Gamma correction for PNG visualization           | `gamma=1`                 |
| `inv`     | Invert PNG grayscale visualization               | `inv=1`                   |

## More Command Examples

Capture depth as `.npy` with PNG visualization:

```text
shoot dir=E:/dump seq=12 fmt=npy base=depth_ png=1
```

Capture depth as `.pfm`:

```text
shoot dir=E:/dump seq=12 fmt=pfm base=depth_ png=0
```

Capture with visualization range:

```text
shoot dir=E:/dump seq=12 fmt=npy base=depth_ png=1 vmin=0 vmax=5000 gamma=1 inv=0
```

Select a specific capture component:

```text
shoot dir=E:/dump seq=3 fmt=npy cap=DepthCapture follow=1
```

Select a specific render target:

```text
shoot dir=E:/dump seq=3 fmt=npy rt=RT_DepthMeters_1080p follow=1
```

Use a custom log file:

```text
shoot dir=E:/dump seq=5 fmt=npy base=depth_ log=E:/dump/FastDepthTap.log
```

## Python UDP Example

You can trigger capture from Python:

```python
import socket

cmd = "shoot dir=E:/dump seq=1 fmt=npy base=depth_ png=1 follow=1"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(cmd.encode("utf-8"), ("127.0.0.1", 7779))
sock.close()
```

Batch capture example:

```python
import socket
import time

HOST = "127.0.0.1"
PORT = 7779
OUT_DIR = "E:/dump"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

for i in range(1, 101):
    cmd = f"shoot dir={OUT_DIR} seq={i} fmt=npy base=depth_ png=1 follow=1"
    sock.sendto(cmd.encode("utf-8"), (HOST, PORT))
    time.sleep(0.2)

sock.close()
```

## Output Files

If the command is:

```text
shoot dir=E:/dump seq=1 fmt=npy base=depth_ png=1
```

The output files will be:

```text
E:/dump/depth_0001.npy
E:/dump/depth_0001_vis.png
```

If the command is:

```text
shoot dir=E:/dump seq=1 fmt=pfm base=depth_
```

The output file will be:

```text
E:/dump/depth_0001.pfm
```

## Logging

By default, the plugin writes logs to:

```text
Saved/Logs/FastDepthTap.log
```

The log records information such as:

* Received UDP command
* Selected capture component
* Selected render target
* Render target size and format
* Capture camera position and rotation
* Player camera position and rotation
* Capture and flush time
* Output file path
* PNG export status

## Console Variables

The plugin includes the following console variables:

```text
fdt.ForceFinalColor
```

Forces `CaptureSource` to `FinalColorHDR`.

```text
fdt.LogToFile
```

Controls whether the plugin writes logs to a dedicated file.

## Notes

* This plugin is developed for Unreal Engine 4.27.
* AirSim should be installed before using this plugin.
* The UDP socket is bound to localhost only: `127.0.0.1:7779`.
* The plugin works in PIE or Game world.
* The selected `SceneCaptureComponent2D` must have a valid `TextureRenderTarget2D`.
* Depth data is exported as 32-bit floating point values.
* If the render target format is `RTF_R32f`, the plugin temporarily uses an internal `RGBA16f` render target to avoid D3D11 readback issues.

## Recommended Use Cases

* AirSim depth data collection
* Synthetic dataset generation
* Computer vision research
* Depth estimation model training
* Robotics simulation
* Camera pose and depth map logging
* Automated Unreal Engine data capture

## Repository Description

A lightweight Unreal Engine 4.27 plugin for AirSim-based projects, providing UDP-controlled depth capture, CPU readback, `.npy` / `.pfm` export, optional PNG visualization, and camera pose logging.

## License

Please add your license information here.
