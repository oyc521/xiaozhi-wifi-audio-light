把像素表情 PNG 放这里，然后用脚本导入。

用法：
  1) 文件名 = 表情名（见下），建议 40x40，黑图案/白底（或透明底黑图案）。
     neutral.png  happy.png  laughing.png  loving.png  cool.png
     sad.png  crying.png  angry.png  surprised.png  sleepy.png
     thinking.png  confused.png  winking.png
  2) 想让某表情眨眼，再加一张 <名字>_blink.png（闭眼版）。
  3) 运行：
       python scripts/import_oyc_faces_png.py
     然后重新编译：idf.py build
  4) 预览图会导出到 build/faces_preview/_sheet.png

说明：
- 只给 <name>.png 时，睁眼=闭眼（该表情不眨眼）。
- 没提供的表情会保留脚本自动生成的那版。
- 图会比 40x40 自动缩放（LANCZOS）；追求像素锐利请直接给 40x40。
