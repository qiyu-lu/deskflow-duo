# Deskflow 双机套件

这是一个基于 [Deskflow](https://github.com/deskflow/deskflow) 的个人功能分支，
并非 Deskflow 官方发布版本。输入广播功能位于
[`feature/mouse-broadcast`](https://github.com/qiyu-lu/deskflow/tree/feature/mouse-broadcast)，
一键配对与文件同步位于
[`feature/unified-setup`](https://github.com/qiyu-lu/deskflow/tree/feature/unified-setup)。

## 主要改动

- 一次配对完成两台电脑的全部设置：键鼠共享布局、证书互信、同步目录。两边各显示一个六位数字核对即可。
- 内置 Syncthing：安装包自带，由 Deskflow 启动和管理，使用独立的配置目录和端口，不影响已有的 Syncthing。
- 简洁主页：只保留连接状态、输入广播开关、同步状态和同步目录；原来的完整界面在"高级 → 显示完整界面"。
- 输入广播：服务端可以把鼠标移动、点击、滚轮和原始键盘按键广播到另一台电脑，鼠标与键盘可独立开关，支持快捷键。
- 文件同步的高级设置直接打开 Syncthing 的网页界面，不重复造轮子。

使用说明请查看
[两台电脑一键配对指南](../docs/QuickStart.zh_CN.md)，
广播功能的细节见
[键鼠广播使用指南](../docs/InputBroadcastGuide.zh_CN.md)。

## 下载

当前预览版：
[Deskflow Input Broadcast Preview 0.2.0](https://github.com/qiyu-lu/deskflow/releases/tag/input-broadcast-v0.2.0)

| 系统 | 下载 |
| --- | --- |
| Windows x64 | [`deskflow-input-broadcast-v0.2.0-windows-x64.zip`](https://github.com/qiyu-lu/deskflow/releases/download/input-broadcast-v0.2.0/deskflow-input-broadcast-v0.2.0-windows-x64.zip) |
| Ubuntu x64（DEB） | [`deskflow-input-broadcast-v0.2.0-ubuntu-x64.zip`](https://github.com/qiyu-lu/deskflow/releases/download/input-broadcast-v0.2.0/deskflow-input-broadcast-v0.2.0-ubuntu-x64.zip) |
| Ubuntu x64（Flatpak） | [`deskflow-input-broadcast-v0.2.0-flatpak-x64.zip`](https://github.com/qiyu-lu/deskflow/releases/download/input-broadcast-v0.2.0/deskflow-input-broadcast-v0.2.0-flatpak-x64.zip) |

下载对应平台的 ZIP 并解压后再安装。请不要选择 Release 页面底部由 GitHub
自动生成的 `Source code (zip)` 或 `Source code (tar.gz)`，它们只包含源代码，不是安装包。

## 上游项目与许可证

- 官方项目：[deskflow/deskflow](https://github.com/deskflow/deskflow)
- 本分支保留 Deskflow 的 [GPL-2.0 许可证及 OpenSSL 例外](../LICENSE)。
