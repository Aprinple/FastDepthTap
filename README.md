# FastDepthTap

FastDepthTap is a lightweight Unreal Engine 4.27 runtime plugin for on-demand depth capture. It listens for local UDP commands, triggers a `SceneCaptureComponent2D`, reads the render target back to CPU memory, and exports depth data as `.npy` or `.pfm` files. It also supports optional grayscale PNG visualization and camera pose logging.

This plugin is designed for computer vision, robotics simulation, dataset generation, depth estimation experiments, and Unreal-based synthetic data workflows.

## Features

* Runtime UE4.27 plugin
* Local UDP command control
* On-demand `SceneCapture2D` depth capture
* CPU readback from `TextureRenderTarget2D`
* Export depth maps as `.npy` or `.pfm`
* Optional PNG visualization output
* Optional depth visualization parameters:

  * `vmin`
  * `vmax`
  * `gamma`
  * `inv`
* SceneCapture selection by component name or render target name
* Optional camera-follow mode
* Player camera pose and capture pose logging
* File logging under `Saved/Logs/FastDepthTap.log`

## UDP Command Format

The plugin listens on:

```text
127.0.0.1:7779
```

Basic command:

```text
shoot dir=E:/dump seq=1 fmt=npy base=depth_ png=1 follow=1
```

Example with visualization range:

```text
shoot dir=E:/dump seq=12 fmt=npy base=depth_ png=1 vmin=0 vmax=5000 gamma=1 inv=0
```

Example with specific capture component or render target:

```text
shoot dir=E:/dump seq=3 fmt=pfm cap=DepthCapture rt=RT_DepthMeters_1080p follow=1
```

## Command Parameters

| Parameter | Description                                              |
| --------- | -------------------------------------------------------- |
| `dir`     | Output directory                                         |
| `seq`     | Output sequence number                                   |
| `fmt`     | Depth format: `npy` or `pfm`                             |
| `base`    | Output filename prefix                                   |
| `log`     | Custom log file path                                     |
| `cap`     | SceneCaptureComponent2D name substring                   |
| `rt`      | RenderTarget exact name                                  |
| `follow`  | Whether capture camera follows player camera, `1` or `0` |
| `png`     | Whether to export grayscale visualization PNG            |
| `vmin`    | Minimum visualization depth value                        |
| `vmax`    | Maximum visualization depth value                        |
| `gamma`   | Gamma correction for visualization                       |
| `inv`     | Invert grayscale visualization, `1` or `0`               |

## Output Files

For command:

```text
shoot dir=E:/dump seq=1 fmt=npy base=depth_ png=1
```

The plugin outputs:

```text
E:/dump/depth_0001.npy
E:/dump/depth_0001_vis.png
```

If `fmt=pfm`, the depth file becomes:

```text
E:/dump/depth_0001.pfm
```

## Notes

* The plugin is intended for Unreal Engine 4.27.
* It runs as a Runtime plugin.
* UDP is bound to localhost only: `127.0.0.1:7779`.
* If the selected render target uses `RTF_R32f`, the plugin temporarily switches to an `RGBA16f` render target internally to avoid D3D11 readback issues.
* Depth values are saved as 32-bit floating point data.
* Logs include capture pose, player camera pose, render target configuration, capture time, and output file information.

## Typical Use Cases

* Synthetic depth dataset generation
* Unreal Engine computer vision data export
* Depth estimation model testing
* Robotics simulation
* Camera pose and depth capture experiments
* Batch capture controlled from Python or external tools
