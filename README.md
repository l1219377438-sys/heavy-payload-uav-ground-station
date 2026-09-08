# heavy-payload-uav-ground-station

`heavy-payload-uav-ground-station` is a Qt 6 desktop ground-control application for MAVLink-compatible drones. It includes serial-port communication, mission planning, RTK/NTRIP configuration, telemetry parsing, attitude display, speech feedback, and multi-drone formation controls.

> This is an experimental project. Validate every command in a safe test environment before using it with an aircraft.

## Requirements

- CMake 3.16 or newer
- A C++ compiler supported by Qt 6 (MSVC is used by the existing Windows build)
- Qt 6.5 or newer with these modules:
  - Quick
  - SerialPort
  - TextToSpeech
  - WebEngineQuick

## Build

From the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable is produced by the CMake target `apprebulid`.

## 高德地图启动

1. 直接打开新版地面站，程序会自动启动内置地图服务，退出时自动关闭，无需 Python 或手动打开 `server.bat`。
2. 默认地址为 `http://127.0.0.1:8000/gaode.html`；端口被占用时自动选择空闲端口并传给 QML，不会干扰已有服务。实际地址会记录在程序的 `[Map]` 启动日志中。
3. `gaode.html` 内的 `GAODE_CONFIG` 已填入本次提供的高德 Key；如果高德控制台给这个 Web 端（JS API）Key 配有安全密钥，将它填入 `securityJsCode`，然后重新加载。

地图仍为单个 `gaode.html`，样式、脚本和 Key 配置均已内嵌。构建时会将地图打包进程序，移动程序后也能读取内置地图。如果想替换地图或修改 Key，将修改后的 `gaode.html` 放在 exe 同目录，重启程序即可优先使用该文件；否则修改源文件后重新编译。服务仅监听本机并提供地图页面。

`server.bat` 保留用于独立浏览器预览：将它与 `gaode.html` 放在同一目录后运行，再访问 `http://localhost:8000/gaode.html`。正常使用地面站不需要运行它。

地图支持 1～10 号机的位置和海拔、最多 1000 点/机的轨迹、卫星图、跟随指定飞机、显示全部、点击选点、编队预览以及任务航点连线。超过 10 秒未更新的飞机标记会变淡。地图工具位于右侧，可点击标题折叠。

点击地图将 **GCJ-02 纬度、经度** 回填到 QML 航点输入框；点击本身不会添加或上传任务。QML 的“添加航点”仍调用现有转换函数得到 WGS-84 后加入任务列表；添加、下载和清除任务同步刷新地图预览。普通浏览器没有 Qt 桥，只显示选取坐标。

页面接口（参数顺序不要调换）：

| JavaScript 接口 | 坐标和高度约定 |
| --- | --- |
| `updateDronePosition(id, lat, lon, alt)` | C++ 已转换的 GCJ-02；海拔米；页面不再转换 |
| `updateDronePositionWithFormation(id, lat, lon, offsetZ)` | WGS-84；页面转换为 GCJ-02；高度是编队相对高度差 |
| `setMissionWaypoints([{lat, lon, ...}])` | WGS-84 航点；空数组清除预览 |
| `bridge.sendCoordinates(lat, lon)` | 页面通过 Qt WebChannel 调用；GCJ-02 |

编队预览需要先收到 1 号机 GPS 位置。地图加载前的位置会缓存，QML 每 100 ms 批量发送更新。编队模拟使用独立虚线标记，清除模拟不会清除真实飞机。此页面为二维俯视图，垂直队形会在水平位置重合，高度差显示在模拟标记上。

验证：`node --test tests/gaode.test.cjs`（无需联网、无需连接飞机）。用 `-DBUILD_MAP_SERVER_TESTS=ON` 配置 CMake 后，可构建并运行 `map_server_test` 验证 HTTP 服务、端口冲突与退出释放。高德底图需要联网；如加载失败，检查 Key 类型、域名白名单、安全密钥及程序的地图启动日志。

高德参考：[JS API 快速上手](https://developer.amap.com/api/javascript-api-v2/getting-started)、[安全密钥配置](https://developer.amap.com/api/javascript-api-v2/guide/abc/jscode)。

## Source layout

- `Main.qml` — primary Qt Quick interface
- `main.cpp` — application entry point and QML type registration
- `mavlink/` — MAVLink C headers used to decode and create protocol messages
- `*controller*`, `missionplanner.*`, `rtkclient.*` — flight-control, mission, and RTK-related features

## Safety

This software can interact with unmanned aircraft. Keep propellers clear, use a simulator or bench setup first, and retain an independent means of safely taking control.

## License

This project is licensed under the [MIT License](LICENSE).
