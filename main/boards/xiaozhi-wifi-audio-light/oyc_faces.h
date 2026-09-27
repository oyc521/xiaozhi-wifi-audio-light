#ifndef OYC_FACES_H
#define OYC_FACES_H

#include <stdbool.h>
#include <string.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 返回 40x40 A8 像素表情（blink=true 为闭眼帧）；未识别的情绪回退 neutral。
 * 由 scripts/gen_oyc_faces.py 生成，实现在 oyc_faces.c。 */
const lv_image_dsc_t* oyc_face_get(const char* emotion, bool blink, bool big);

#ifdef __cplusplus
}
#endif

#endif // OYC_FACES_H
