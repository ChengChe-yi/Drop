# Drop — 原神掉落物光柱屏蔽插件

## 功能

通过 Hook 游戏内函数，按黑名单/白名单屏蔽指定掉落物的光柱和拾取提示框。

## 使用

1. 用注入器把 `Drop.dll` 注入 YuanShen.exe
2. `Drop.ini` 跟 Drop.dll 同目录
3. 改配置后等 1 秒自动生效（热重载）只对新的掉落物生效

## 配置文件 Drop.ini

```ini
[PillarFilter]       总开关          1=开 0=关
[SuppressPickupBar]  屏蔽拾取栏      1=光柱+拾取栏全屏蔽  0=只屏蔽拾取栏
[FilterMode]         过滤模式        blacklist / whitelist
[Blacklist]          黑名单          逗号分隔的游戏对象名
[HotReload]          热重载间隔      毫秒(默认1000)
```

## 配置项

| 配置 | 说明 |
|------|------|
| `PillarFilter` | 屏蔽总开关。0=全部放行 |
| `SuppressPickupBar` | 0=只屏蔽拾取提示框，保留光柱；1=光柱和拾取栏一起屏蔽 |
| `FilterMode` | `blacklist` = 只屏蔽列表中的；`whitelist` = 只放行列表中的 |
| `Blacklist` | 要屏蔽的 GameObject 名字，逗号分隔。含 "Relic" 的自动放行 |

## 项目结构

```
Drop/
├── Patterns.h        ← 偏移地址（游戏更新时改这里）
├── Config/
│   ├── Config.h/cpp  ← INI 读取 + 热重载
│   └── Drop.ini      ← 用户配置
├── Hooks.h/cpp       ← Hook 逻辑
├── dllmain.cpp       ← 入口
├── MinHook/          ← MinHook 源码
└── Memory.h/cpp      ← 工具函数
```

## 构建

Visual Studio 2022，打开 `Drop.slnx`，选 Release/x64 编译。

## 注意

- 只支持 x64
- 偏移地址随游戏版本更新可能变化
- 第一次使用建议先开 DebugView 看日志确认名字读取正常
