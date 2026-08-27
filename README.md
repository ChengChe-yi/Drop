# Drop

> 自定义屏蔽掉落物拾取提示框的轻量插件

<div align="center">

![Version](https://img.shields.io/badge/version-7.0.2-blue?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Windows-0078d4?style=flat-square&logo=windows)
![Status](https://img.shields.io/badge/status-beta-orange?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)

一款拦截拾取提示框 UI 的插件,支持白名单与热重载。

</div>

---

## ⚠️ 免责通知

> **本插件仍处于测试阶段**,强烈不建议在正式服或主要账号上使用。
>
> 使用本插件产生的任何后果(包括但不限于封号、数据异常等)均由使用者自行承担。


---

## 📦 安装

略；支持主流启动器的插件安装

安装完成后将以下文件部署到DLL同级目录:

```
目录/
├── Drop.dll                 # 插件主 DLL
├── Config.ini               # 主配置文件
└── Whitelist.ini            # 白名单配置文件
```

---

## 🚀 使用方法

### 配置项 (`Config.ini`)

```ini
[PickupSuppress]
Name  = 拾取提示框屏蔽
Type  = bool
Value = 1   ; 1 = 开启,0 = 关闭

[Log]
Name  = 日志开关
Type  = bool
Value = 1   ; 1 = 输出日志,0 = 静默
```

### 白名单 (`Whitelist.ini`)

每行一个物品名,**必须与游戏当前语言保持一致**,精确匹配:

```ini
[PickupSuppress]
; 在此列表中的物品即使图标匹配也不会被拦截
史莱姆凝液
```

---


## 🤝 反馈与贡献

- 🐛 提交 Issue:遇到崩溃、漏拦、误拦等情况
- 💡 提出建议:功能优化或新特性想法
- 🔧 提交 PR:欢迎改进代码与文档

---

<div align="center">

**❤️ by [ChengChe-yi](https://github.com/ChengChe-yi)**

</div>
