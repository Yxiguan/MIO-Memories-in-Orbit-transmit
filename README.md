# MIO 轨道记忆 (Memories in Orbit) - 传送 Mod

这是一个为游戏《MIO: Memories in Orbit》(进程名 `mio.exe`) 开发的内置传送工具。本项目基于 DirectX 12 Hook (ImGui + Kiero) 实现，通过读写游戏内存坐标，支持玩家进行坐标记录、备注、管理和瞬移。

## ✨ 功能特性

- **内置中文菜单**：使用 ImGui 并在内部加载了微软雅黑字体(`msyh.ttc`)，完全支持中文字符的显示和备注输入。
- **实时坐标显示**：在菜单中实时获取并显示角色当前的 `X` 和 `Y` 坐标（内存基址 `0x10EFF48`，偏移 `0x18/0x1C`），并提示当前内存坐标的读取状态。
- **坐标记录与备注**：玩家可以将当前所在位置保存到列表中，并为每个位置添加自定义文本备注（例如：Boss房间、隐藏宝箱等）。
- **一键瞬移**：选择列表中的任意坐标，即可瞬间将角色传送到选中位置。
- **数据持久化**：所有保存的坐标及备注会自动存储至游戏目录下的 `TeleportLocations.txt` 文件，下次启动游戏自动读取。
- **坐标管理**：可以在菜单内随时删除不再需要的坐标记录。

## 🎮 快捷键说明

- **`F7`** : 唤出 / 隐藏内置传送菜单。
- **`F6`** : 快速传送到当前列表中选定的位置。

## 🛠️ 编译与构建

本项目基于 CMake 进行构建。

### 环境要求

- Windows 10/11
- Visual Studio 2019/2022 (或 VSCode)
- [DirectX SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
- CMake 3.15 或更高版本
- Git

### 构建步骤 (以命令行编译为例)

1. 克隆本仓库（请确保拉取子模块）：
   ```bash
   git clone --recurse-submodules [你的仓库地址]
   cd MIO-Memories-in-Orbit-transmit
   ```

2. 使用 CMake Preset 构建系统（以 VS2022 Release 为例）：
   ```bash
   cmake --preset windows-x64-release-2022
   cmake --build --preset windows-x64-release-2022
   ```

编译完成后，DLL 文件将生成在 `bin/Release` 目录下。直接将其 DLL 注入到 `mio.exe` 进程即可使用此 Mod。

