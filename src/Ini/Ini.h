#pragma once
#include <cstddef>

// 轻量 INI 解析原语，供 Config（key=value 设置）、Whitelist/Blacklist
// （每行一条目的名单）共用。编码假设 UTF-8（自动跳过 BOM）。
//
// 文件格式约定：
//   [Section]            段头（行首，忽略行首空白）
//   key = value          键值对（'=' 两侧去空白；value 截断于 ';'/'#'）
//   entry                名单行：整行去空白后即条目
//   ; # 注释             行首注释；value 内的 ';'/'#' 亦截断
namespace Ini
{
    // 整读文件到堆缓冲（自动跳过 UTF-8 BOM），调用方 delete[] 释放。
    // 失败返回 nullptr。maxBytes 非 0 时超限直接放弃（防失控文件）。
    //（不叫 ReadFile：避免与 Win32 ReadFile 在命名空间内发生遮蔽。）
    char* LoadFile(const wchar_t* path, size_t* outSize, size_t maxBytes = 0);

    // 在 [section] 块内查 key 的值；找到写入 out（NUL 结尾）并返回 true。
    bool GetValue(const char* content, const char* section, const char* key,
                  char* out, size_t outChars);

    // 解析布尔值：1/true/yes/on 与 0/false/no/off（不区分大小写），
    // 其余取 defaultVal。
    bool ParseBool(const char* value, bool defaultVal);

    // 名单行回调：value 为去空白后的整行（空行/注释行不回调）。
    // 返回 false 可提前终止遍历。
    typedef bool (*EntryCallback)(const char* value, void* ctx);

    // 遍历 [section] 块内的每个名单条目；段不存在返回 false。
    bool ForEachEntry(const char* content, const char* section,
                      EntryCallback cb, void* ctx);
}
