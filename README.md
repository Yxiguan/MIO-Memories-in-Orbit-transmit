# 秘奥 秘宇奥忆 (MIO: Memories in Orbit) - 传送 Mod

这是一个为游戏《MIO: Memories in Orbit》 开发的内置传送工具。本项目基于 DirectX 12 Hook (ImGui + Kiero) 实现。

## ✨ 功能特性

- **现代化深色 UI**：重构了基于 ImGui 的内置菜单，采用深色渐变与圆角设计，支持中文显示（微软雅黑）。
- **坐标管理系统**：
  - 实时显示角色坐标（支持读取状态实时反馈）。
  - 支持坐标保存、备注编辑及数据持久化（`TeleportLocations.txt`）。
  - 一键传送到选中位置。
- **高级修改器**：
  - **飞行模式**：支持 WASD 空中移动及 Shift 加速。
  - **能量锁定**：自动将能量锁定在 100%。

## 🎮 快捷键说明

- **`F7`** : 开启 / 关闭主菜单。
- **`F6`** : 快速传送到当前列表中选定的位置。
- **`F9`** : 卸载并清理 DLL。

## 🏗️ 项目架构 (重构版)

项目采用了模块化解耦设计，职责分明：

- **`dll/src/game`**: 封装底层内存读写逻辑与地址偏移。
- **`dll/src/features`**: 实现核心功能逻辑（传送、飞行、热键）。
- **`dll/src/ui`**: 现代样式的界面渲染逻辑。
- **`dll/src/hooks`**: 纯粹的 D3D12 API 钩子管理。

## 🛠️ 编译与构建

1. **环境**: Windows 10/11, VS 2022, CMake 3.24+。
2. **构建**:
   ```ps1
   cmake -B out -S .
   cmake --build out --config Release
   ```
   编译后的 `MIO-Memories-in-Orbit-transmit-ImGui.dll` 将生成于 `.bin/Release` 目录。
