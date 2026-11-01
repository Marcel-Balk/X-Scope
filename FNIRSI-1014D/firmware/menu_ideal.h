//----------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef MENU_IDEAL_H
#define MENU_IDEAL_H

#include "types.h"
#include "variables.h"

//----------------------------------------------------------------------------------------------------------------------------------

typedef struct tagIdealMenuItem
{
  const char *label;
  void      (*value)(char *text);         //Current value as text, 0 for rows without a value
  void      (*change)(int32 direction);   //Left / right on the row, 0 when the value can not be changed
  void      (*action)(void);              //OK on the row, 0 means OK steps the value forward
} IDEALMENUITEM, *PIDEALMENUITEM;

typedef struct tagIdealMenu
{
  const char          *title;
  const IDEALMENUITEM *items;
  uint32               count;
} IDEALMENU, *PIDEALMENU;

//----------------------------------------------------------------------------------------------------------------------------------

#define IDEAL_MENU_XPOS_MAIN        192
#define IDEAL_MENU_XPOS_KEY         200
#define IDEAL_MENU_YPOS              60
#define IDEAL_MENU_WIDTH            320
#define IDEAL_MENU_ROW_HEIGHT        22
#define IDEAL_MENU_STACK_DEPTH        4

//----------------------------------------------------------------------------------------------------------------------------------

extern const IDEALMENU ideal_display_menu;
extern const IDEALMENU ideal_utilities_menu;
extern const IDEALMENU ideal_generator_menu;
extern const IDEALMENU ideal_channel_menu;

void   ideal_menu_open(const IDEALMENU *menu, uint32 frommainmenu);
void   ideal_menu_handle_navigation(void);
uint32 ideal_menu_active(void);
uint32 ideal_menu_layout(void);
void   ideal_menu_draw(void);

void   sm_open_display_menu(void);
void   sm_open_utilities_menu(void);
void   sm_open_generator_menu(void);

void   ideal_draw_main_menu_label(uint32 item, uint32 xpos, uint32 ypos);

void   ideal_draw_fft(PCHANNELSETTINGS settings, uint32 line);
void   ideal_draw_timing_overlay(void);

#endif /* MENU_IDEAL_H */
