#ifndef LV_CONF_H
#define LV_CONF_H

// 第 12 课只启用 RGB565 和本课使用的控件。
#define LV_COLOR_DEPTH 16

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_SLIDER 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_TABVIEW 1

#endif
