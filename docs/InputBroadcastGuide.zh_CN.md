# Deskflow 键鼠广播使用指南

本文面向 Ubuntu X11 服务端与 Windows 客户端。键盘广播按原始按键工作，建议两端使用相同键盘布局；不保证不同输入法之间的文本完全一致。

## 下载与安装

从 [Input Broadcast Preview 0.2.0](https://github.com/qiyu-lu/deskflow-duo/releases/tag/input-broadcast-v0.2.0) 下载对应平台的 ZIP：

- Windows：`deskflow-input-broadcast-v0.2.0-windows-x64.zip`
- Ubuntu DEB：`deskflow-input-broadcast-v0.2.0-ubuntu-x64.zip`
- Ubuntu Flatpak：`deskflow-input-broadcast-v0.2.0-flatpak-x64.zip`

不要下载 Release 页面自动生成的 `Source code`，它不是安装包。安装前退出旧版 Deskflow；Ubuntu 的 DEB 和 Flatpak 请选择一种，不要同时运行。

Windows 解压后运行 `.msi`。Ubuntu DEB 可执行：

```bash
sudo apt install ./deskflow-input-broadcast-*.deb
```

Flatpak 可执行：

```bash
flatpak install --user ./deskflow-*-linux-x86_64.flatpak
flatpak run org.deskflow.deskflow
```

## 使用

1. Ubuntu 选择 Server，Windows 选择 Client，并先确认普通键鼠共享正常。
2. 客户端连接后，在服务端主界面的 `Targets` 中选择目标；`All` 表示所有在线客户端。
3. 分别勾选 `Mouse` 或 `Keyboard` 开启对应广播，再次取消勾选即可关闭。
4. 服务端状态栏显示正在广播的模式，客户端状态栏和托盘显示正在接收的模式。

需要快捷键时，在 `Configure Server → Hotkeys` 中添加 `Control mouse broadcasting` 或 `Control keyboard broadcasting`，通常选择 `Toggle` 即可。

## 行为与限制

- 鼠标广播同步移动、点击和滚轮，并在广播期间把光标保留在服务端。Ubuntu 服务端要求只启用一个显示器。
- 键盘广播同步按下、长按重复和释放；光标离开服务端时会自动关闭。
- 锁屏、进入屏保或最后一个目标断开时会停止广播并释放远端按键。
- 当前重点验证 Ubuntu X11 → Windows；可用 `echo $XDG_SESSION_TYPE` 确认会话类型。Wayland 暂不承诺兼容。

若开关有提示但客户端没有输入，请确认两端均来自同一 Release、目标名称正确，并查看日志中的 `mouse broadcasting`、`keyboard broadcasting` 或 `raw key` 信息。
