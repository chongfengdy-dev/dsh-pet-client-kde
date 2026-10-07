// 宠物状态色
//   蓝 = 空闲待命（对话窗口可见）
//   黑 = 对话窗口已收起 / 未打开
//   橙 = 提问中 / 等待授权（400ms 心跳闪烁）
//   绿 = 回复完成、用户尚未查看（常亮，2026-08-27 主定：绿不闪）
#pragma once

enum class PetColor {
    Blue = 0,
    Black = 1,
    Orange = 2,
    Green = 3,
};

inline int petColorIndex(PetColor c)
{
    const int i = int(c);
    return (i >= 0 && i <= 3) ? i : 0;
}
