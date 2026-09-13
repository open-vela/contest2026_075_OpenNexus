# FocusMate 像素图片资源规格

## 1. 适用范围

本规范用于 FocusMate 在 390 x 450 逻辑屏幕上的像素画、图标和简单动画资源。

目标：

- 保持统一的像素风
- 在 LVGL 9.1 中稳定显示
- 控制 Flash 和 RAM 占用
- 方便后续替换和扩展

## 2. 源图片格式

| 项目 | 要求 |
|---|---|
| 文件格式 | PNG |
| 色彩模式 | PNG-24 或 PNG-32 |
| 背景 | 透明背景 |
| 透明通道 | 只使用全透明或完全不透明，禁止半透明羽化边缘 |
| 色彩数量 | 建议 4～16 色，最多 32 色 |
| 抗锯齿 | 关闭 |
| 渐变 | 禁止 |
| 阴影 | 使用硬边像素阴影，禁止模糊阴影 |
| 缩放 | 禁止非整数缩放 |
| 线条 | 1px 或 2px 硬边像素线 |
| 画布 | 每个资源使用独立画布，不留无用空白 |

透明区域必须符合：

```text
Alpha = 0
```

不透明区域必须符合：

```text
Alpha = 255
```

禁止边缘出现 Alpha 为 1～254 的像素。

## 3. 推荐尺寸

| 用途 | 尺寸 |
|---|---|
| 小图标、问号、勾选状态 | 24 x 24 |
| 普通按钮图标 | 32 x 32 |
| 功能图标、文件夹、时钟 | 48 x 48 |
| 首页机器人、奖杯、主视觉 | 64 x 64 |
| 大号主视觉 | 96 x 96，谨慎使用 |
| 两帧动画单帧 | 48 x 48 或 64 x 64 |

建议优先使用 32 x 32、48 x 48、64 x 64。

## 4. 视觉规范

推荐背景色：

```text
#101418
```

推荐主色：

```text
白色   #FFFFFF
绿色   #30A030
蓝色   #2070D0
橙色   #D08020
红色   #D02020
灰色   #555C64
```

像素画在深色背景上必须满足：

- 主体轮廓清晰
- 不使用过暗的黑色描边
- 不依赖细小单像素细节表达语义
- 缩小到目标尺寸后仍能识别

## 5. 命名规范

源文件命名格式：

```text
<场景>_<对象>_<尺寸>.png
```

示例：

```text
home_robot_64.png
home_voice_48.png
taskcheck_question_32.png
duration_clock_48.png
focus_hourglass_48.png
review_checkbox_32.png
library_folder_48.png
complete_trophy_64.png
```

C 语言符号命名：

```text
img_home_robot_64
img_home_voice_48
img_taskcheck_question_32
img_duration_clock_48
img_focus_hourglass_48
img_review_checkbox_32
img_library_folder_48
img_complete_trophy_64
```

## 6. 资源目录

源图片：

```text
app/focusmate/assets/images/
```

LVGL 生成文件：

```text
app/focusmate/ui/focus_ui_assets.c
app/focusmate/ui/focus_ui_assets.h
```

每个资源可以配套一个元数据文件：

```text
app/focusmate/assets/images/home_robot_64.json
```

元数据示例：

```json
{
  "id": "home_robot",
  "source": "home_robot_64.png",
  "width": 64,
  "height": 64,
  "colors": 12,
  "transparent": true,
  "target_format": "RGB565A8",
  "usage": ["FM_IDLE"]
}
```

## 7. LVGL 输出格式

推荐优先级：

1. 带透明通道资源：
   ```text
   RGB565A8
   ```
2. 无透明背景资源：
   ```text
   RGB565
   ```
3. 颜色不超过 16 色的图标：
   ```text
   I4 或转换工具支持的索引色格式
   ```
4. 单色图标：
   ```text
   I1
   ```

像素画不要使用抗锯齿或运行时缩放。

## 8. 屏幕放置建议

当前 UI 主要区域：

```text
0～80      标题和状态
80～160    计时器、进度条
160～340   主提示区域
340～450   底部按钮
```

建议放置区域：

| 页面 | 图片 | 建议位置 |
|---|---|---|
| 主页 | 机器人 | 顶部居中，y=70～140 |
| 任务选择 | 问号或清单 | 标题右侧 |
| 时间选择 | 时钟 | 任务标题右侧 |
| 专注页 | 沙漏 | 计时器右侧 |
| 完成确认 | 勾选框 | 提示文字上方 |
| 任务库 | 文件夹 | 标题左侧 |
| 完成页 | 奖杯或星星 | 报告上方 |

图片不能遮挡任务标题、计时器、按钮和触摸区域。

## 9. 触摸区域

图片本身不作为唯一触摸目标。

如果需要点击：

- 图片外层必须有独立按钮
- 触摸区域不小于 48 x 48
- 图片与按钮边缘至少保留 4px 间距

## 10. 动画规范

第一版优先使用静态图片。

需要动画时：

- 每个动画最多 4 帧
- 每帧尺寸一致
- 帧与帧之间只改变必要像素
- 循环周期建议 600～1200ms
- 不使用高帧率动画
- 动画不能持续占用大量 CPU

## 11. 验收清单

- [ ] 源文件为 PNG
- [ ] 背景透明
- [ ] 没有半透明羽化边缘
- [ ] 颜色数量不超过 32
- [ ] 目标尺寸下清晰可辨
- [ ] 文件名符合命名规范
- [ ] 已记录元数据
- [ ] 已在 390 x 450 模拟器检查
- [ ] 未遮挡按钮和文字
- [ ] 已验证 RGBA/RGB565A8 转换结果
- [ ] 已在目标设备上检查实际显示效果

## 12. 交付格式

每个图片资源交付以下内容：

```text
<name>.png
<name>.json
```

最终统一转换并汇总到：

```text
app/focusmate/ui/focus_ui_assets.c
app/focusmate/ui/focus_ui_assets.h
```
