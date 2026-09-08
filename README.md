# heavy-payload-uav-ground-station

中文：这是一个基于 Qt 6 的 MAVLink 无人机桌面地面站，提供串口通信、航点任务、RTK/NTRIP 配置、遥测解析、姿态显示、语音提示和多机编队控制。

English: A Qt 6 desktop ground-control application for MAVLink-compatible drones. It provides serial communication, mission planning, RTK/NTRIP configuration, telemetry parsing, attitude display, speech feedback, and multi-drone formation controls.

> 中文：本项目处于实验阶段。连接飞行器前，请先在安全的仿真或台架环境中验证每一项操作。
>
> English: This is an experimental project. Validate every operation in a safe simulator or bench-test environment before connecting an aircraft.

## 依赖 / Requirements

- CMake 3.16 或更高版本 / CMake 3.16 or later
- Qt 6 支持的 C++ 编译器；现有 Windows 构建使用 MSVC / A C++ compiler supported by Qt 6; the existing Windows build uses MSVC
- Qt 6.5 或更高版本，包含以下模块 / Qt 6.5 or later with these modules:
  - Quick
  - SerialPort
  - TextToSpeech
  - WebEngineQuick
  - Network

## 构建 / Build

在项目目录执行 / From the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

生成的可执行文件目标为 `apprebulid`。

The executable is produced by the `apprebulid` CMake target.

## 高德地图 / Gaode Map

1. 中文：直接启动新版地面站。程序会自动启动内置地图服务，并在退出时关闭；无需 Python 或手动运行 `server.bat`。

   English: Start the current ground station directly. It starts and stops its built-in map service automatically; Python and `server.bat` are not needed for normal use.

2. 中文：默认地图地址为 `http://127.0.0.1:8000/gaode.html`。如果 8000 被占用，程序会自动选择空闲端口并传给 QML，不会影响已有服务。实际地址会写入 `[Map]` 启动日志。

   English: The default map URL is `http://127.0.0.1:8000/gaode.html`. If port 8000 is occupied, the app selects an available local port and passes it to QML without interrupting the existing service. The actual URL appears in the `[Map]` startup log.

3. 中文：`gaode.html` 的 `GAODE_CONFIG` 已包含地图 Key。如果高德控制台为该 Web 端 JS API Key 配置了安全密钥，请填写 `securityJsCode` 后重启程序。

   English: `GAODE_CONFIG` in `gaode.html` contains the map key. If Gaode provides a security key for this Web JS API key, set `securityJsCode` and restart the application.

中文：地图是单个 `gaode.html` 文件，样式、脚本和 Key 配置都已内嵌。构建时它会被打包进程序，因此移动已部署的程序后仍能读取内置地图。若要替换地图或修改 Key，把修改后的 `gaode.html` 放在 exe 同目录并重启；该外部文件会优先于内置版本。地图服务只监听本机并只提供地图页面。

English: The map is a single `gaode.html` file with its styles, scripts, and key configuration embedded. It is compiled into the application, so deployed builds retain the built-in map. To replace the page or change the key, place a modified `gaode.html` next to the exe and restart; that external file takes precedence. The map service listens on loopback only and serves only the map page.

中文：`server.bat` 仅保留给独立浏览器预览。将它与 `gaode.html` 放在同一目录后运行，然后访问 `http://localhost:8000/gaode.html`。正常使用地面站无需运行它。

English: `server.bat` remains available only for standalone browser preview. Put it beside `gaode.html`, run it, then open `http://localhost:8000/gaode.html`. It is not required for normal ground-station use.

### 地图功能 / Map Features

中文：地图支持 1～10 号机的位置与海拔、每架机最多 1000 点的轨迹、卫星图、跟随指定飞机、显示全部、点击选点、编队预览和任务航点连线。超过 10 秒未更新的位置标记会变淡；右侧地图工具可点击标题折叠。

English: The map supports positions and altitude for aircraft 1–10, up to 1,000 track points per aircraft, satellite imagery, aircraft following, fit-to-view, point picking, formation preview, and mission waypoint lines. Markers fade after 10 seconds without an update. Click the map-tools title at the right to collapse it.

中文：点击地图会将 **GCJ-02 纬度、经度** 回填到 QML 航点输入框，但不会自动添加或上传任务。QML 的“添加航点”仍会转换为 WGS-84 后加入任务列表；添加、下载和清除任务都会刷新地图预览。普通浏览器没有 Qt 桥接，只显示选取的坐标。

English: Clicking the map writes **GCJ-02 latitude and longitude** back to the QML waypoint fields, but does not add or upload a mission. QML still converts a newly added waypoint to WGS-84 before adding it to the mission list; add, download, and clear operations refresh the preview. A regular browser has no Qt bridge and only displays the picked coordinate.

接口参数顺序不可调换 / Do not change the parameter order:

| JavaScript interface | 中文说明 | English description |
| --- | --- | --- |
| `updateDronePosition(id, lat, lon, alt)` | C++ 已转换的 GCJ-02，海拔单位为米，页面不再转换。 | GCJ-02 already converted by C++; altitude is in metres and is not converted again. |
| `updateDronePositionWithFormation(id, lat, lon, offsetZ)` | WGS-84；页面转为 GCJ-02；高度为编队相对高度差。 | WGS-84; the page converts it to GCJ-02; height is the formation altitude offset. |
| `setMissionWaypoints([{lat, lon, ...}])` | WGS-84 航点；传入空数组清除预览。 | WGS-84 waypoints; pass an empty array to clear the preview. |
| `bridge.sendCoordinates(lat, lon)` | 页面经 Qt WebChannel 调用，坐标系为 GCJ-02。 | Called by the page through Qt WebChannel; coordinates are GCJ-02. |

中文：编队预览需要先收到 1 号机的 GPS 位置。地图加载前的位置会缓存，QML 每 100 ms 批量发送更新。编队使用独立虚线标记，清除模拟不会清除真实飞机。地图是二维俯视图，垂直队形会在水平位置重合，高度差显示在模拟标记上。

English: Formation preview requires a GPS fix from aircraft 1 first. Positions received before the page is ready are cached, and QML sends batched updates every 100 ms. Formation markers use a separate dashed style; clearing a simulation never removes a real aircraft. The map is a 2D top-down view, so vertical formations overlap horizontally and show their height offsets in the simulated labels.

## 验证 / Validation

中文：`node --test tests/gaode.test.cjs` 不需要联网或连接飞行器。使用 `-DBUILD_MAP_SERVER_TESTS=ON` 配置 CMake 后，可构建并运行 `map_server_test`，验证 HTTP 服务、端口冲突和退出时释放端口。高德底图需要联网；若加载失败，请检查 Key 类型、域名白名单、安全密钥和程序的地图启动日志。

English: `node --test tests/gaode.test.cjs` requires neither network access nor an aircraft connection. Configure CMake with `-DBUILD_MAP_SERVER_TESTS=ON` to build and run `map_server_test`, which checks the HTTP service, port conflicts, and port release on exit. Gaode tiles require network access; if loading fails, check the key type, domain allowlist, security key, and the app's map startup log.

高德参考 / Gaode references: [JS API 快速上手 / Getting started](https://developer.amap.com/api/javascript-api-v2/getting-started), [安全密钥配置 / Security-key configuration](https://developer.amap.com/api/javascript-api-v2/guide/abc/jscode).

## 源码结构 / Source Layout

- `Main.qml` — 主 Qt Quick 界面 / primary Qt Quick interface
- `main.cpp` — 程序入口与 QML 类型注册 / application entry point and QML type registration
- `localmapserver.*` — 内置本机地图 HTTP 服务 / built-in loopback map HTTP service
- `gaode.html` — 单文件地图页面 / single-file map page
- `mavlink/` — MAVLink C 头文件 / MAVLink C headers
- `*controller*`、`missionplanner.*`、`rtkclient.*` — 飞控、任务与 RTK 功能 / flight control, mission, and RTK features

## 安全提示 / Safety

中文：软件可以与无人机交互。请保持螺旋桨周围无人，并在实际飞行前使用模拟器或台架进行测试，同时保留独立的安全接管方式。

English: This software can interact with unmanned aircraft. Keep propellers clear, use a simulator or bench setup before flight, and retain an independent means of safe takeover.

## 许可证 / License

本项目采用 [MIT License](LICENSE)。

This project is licensed under the [MIT License](LICENSE).
