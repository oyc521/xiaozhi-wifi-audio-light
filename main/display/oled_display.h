#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>


class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t *emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

    // 像素表情（仅 128x64 且 EnableCuteFace(true) 时启用）
    bool cute_face_ = false;
    bool big_face_ = false;
    lv_obj_t* face_img_ = nullptr;
    lv_timer_t* face_blink_timer_ = nullptr;
    bool face_blink_ = false;
    char face_emotion_[16] = "neutral";

    void BuildCuteFace();
    void DrawCuteFace();
    static void FaceBlinkTimer(lv_timer_t* t);

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;

    /** 启用程序化 Q 版脸（替换 Font Awesome 情绪图标）。 */
    void EnableCuteFace(bool enable);
    /** 显示 Font Awesome 模式图标（如音乐/氛围），隐藏脸。 */
    void SetModeIcon(const char* fontAwesomeGlyph);
    /** 隐藏模式图标，恢复显示脸。 */
    void ClearModeIcon();

    /** 大脸模式：铺满 128x64，隐藏状态栏与聊天文字，仅显示表情。 */
    void EnableBigFace(bool enable);
};

#endif // OLED_DISPLAY_H
