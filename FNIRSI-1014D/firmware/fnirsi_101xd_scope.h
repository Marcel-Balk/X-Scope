//----------------------------------------------------------------------------------------------------------------------------------

#ifndef FNIRSI_101XD_SCOPE_H
#define FNIRSI_101XD_SCOPE_H

//----------------------------------------------------------------------------------------------------------------------------------

#include "types.h"
#include "port_config.h"

//----------------------------------------------------------------------------------------------------------------------------------

#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   480

#define SCREEN_SIZE     (SCREEN_WIDTH * SCREEN_HEIGHT)

//----------------------------------------------------------------------------------------------------------------------------------

#define CHANNEL1_COLOR         0x00FFFF00   //YELLOW_COLOR
#define REF1_COLOR             0x00FFA500   //ORANGE_COLOR
#define REF2_COLOR             0x00E9967A   //_COLOR
#define REF3_COLOR             0x00FF6347   //_COLOR
#define REF4_COLOR             0x00FF4500   //_COLOR
#define CHANNEL2_COLOR         0x0000FFFF   //BLUE_COLOR
#define REF5_COLOR             0x0087CEFA   //BLUE_COLOR
#define REF6_COLOR             0x001E90FF   //_COLOR
#define REF7_COLOR             0x000000FF   //_COLOR
#define REF8_COLOR             0x00191970   //_COLOR
#define TRIGGER_COLOR          0x0000FF00   //GREEN_COLOR
#define MATH_COLOR             0x00FF00FF 


#define CHANNEL1_TRIG_COLOR    0x00CCCC00   //  
#define CHANNEL2_TRIG_COLOR    0x0000CCCC   //
#define EXTERN_TRIG_COLOR      0x00FF8000

#define XYMODE_COLOR           0x00FF00FF   //MAGENTA_COLOR

#define CURSORS_COLOR          0x0000AA11   //

#define ITEM_ACTIVE_COLOR      0x00EF9311   //

//--- 1013D naming (original) ---
#define BLACK_COLOR            0x00000000
#define DARKGREY_COLOR         0x00181818
#define LIGHTGREY_COLOR        0x00333333
#define MIDLEGREY_COLOR        0x00383838
#define GREY_COLOR             0x00444444
#define LIGHTGREY1_COLOR       0x00606060
#define LIGHTGREY2_COLOR       0x00BBBBBB
#define WHITE_COLOR            0x00FFFFFF
#define LIGHTRED_COLOR         0x00FF4444
#define RED_COLOR              0x00FF0000
#define YELLOW_COLOR           0x00FFFF00
#define PALEYELLOW_COLOR       0x00FFFF80
#define DARKGREEN_COLOR        0x0000BB00
#define GREEN_COLOR            0x0000FF00
#define BLUE_COLOR             0x0000FFFF
#define LDARKBLUE_COLOR        0x006666FF
#define DARKBLUE_COLOR         0x00000078
#define ORANGE_COLOR           0x00FF8000
#define MAGENTA_COLOR          0x00FF00FF

//--- 1014D COLOR_* naming aliases ---
#define COLOR_BLACK             BLACK_COLOR
#define COLOR_WHITE             WHITE_COLOR
#define COLOR_DARK_GREY_1       0x00111111
#define COLOR_DARK_GREY_2       0x00222222
#define COLOR_DARK_GREY_3       0x00333333
#define COLOR_DARK_GREY_4       0x00444444
#define COLOR_DARK_GREY_6       0x00666666
#define COLOR_DARK_GREY_7       0x00777777
#define COLOR_GREY              0x00888888
#define COLOR_LIGHT_GREY_9      0x00999999
#define COLOR_LIGHT_GREY_A      0x00AAAAAA
#define COLOR_RED               0x00FF0000
#define COLOR_GREEN             0x0000FF00
#define COLOR_GLIMMER_GREEN     0x0000BB00
#define COLOR_PHOSPHOR_GREEN    0x0000AA00
#define COLOR_ISLAMIC_GREEN     0x00009900
#define COLOR_PAKISTAN_GREEN    0x00006600
#define COLOR_PHARMACY_GREEN    0x00005500
#define COLOR_DARK_GREEN        0x00002200
#define COLOR_BLUE              0x000000FF
#define COLOR_DARK_BLUE         0x00000055
#define COLOR_YELLOW            0x00FFFF00
#define COLOR_CHARTREUSE_YELLOW 0x00DDDD00
#define COLOR_RIOJA_YELLOW      0x00BBBB00
#define COLOR_CITRUS_YELLOW     0x00AAAA00
#define COLOR_PEA_SOUP          0x00999900
#define COLOR_OLIVE             0x00888800
#define COLOR_VERDUN_GREEN      0x00555500
#define COLOR_DARK_YELLOW       0x00444400
#define COLOR_LIQUORICE         0x00222200
#define COLOR_CYAN              0x0000FFFF
#define COLOR_BRIGHT_TURQUOISE  0x0000DDDD
#define COLOR_IRISH_BLUE        0x0000BBBB
#define COLOR_PERSIAN_GREEN     0x0000AAAA
#define COLOR_LIGHT_BLUE        0x00009999
#define COLOR_DARK_CYAN         0x00008888
#define COLOR_MOSQUE            0x00005555
#define COLOR_SHERPA_BLUE       0x00004444
#define COLOR_STELLAR_EXPLORER  0x00002222
#define COLOR_MAGENTA           0x00FF00FF

#define CONFIRM_WINDOW_BG_COLOR     0x00A04020
#define FILE_NAME_HIGHLIGHT_COLOR   0x00D8B70B
#define FILE_BORDER_COLOR           0x00CC8947

      //Light gray for the buttons
      //display_set_fg_color(0x00303030);

//----------------------------------------------------------------------------------------------------------------------------------

#define TOUCH_STATE_INACTIVE                 0x00
#define TOUCH_STATE_HAVE_DISPLACEMENT        0x01
#define TOUCH_STATE_X_Y_MODE                 0x02
#define TOUCH_STATE_MOVE_CHANNEL_1           0x03
#define TOUCH_STATE_MOVE_CHANNEL_2           0x04
#define TOUCH_STATE_MOVE_TRIGGER_LEVEL       0x05
#define TOUCH_STATE_MOVE_TIME_CURSOR_LEFT    0x06
#define TOUCH_STATE_MOVE_TIME_CURSOR_RIGHT   0x07
#define TOUCH_STATE_MOVE_VOLT_CURSOR_TOP     0x08
#define TOUCH_STATE_MOVE_VOLT_CURSOR_BOTTOM  0x09
#define TOUCH_STATE_MOVE_REF1                0x0A
#define TOUCH_STATE_MOVE_REF2                0x0B
#define TOUCH_STATE_MOVE_REF3                0x0C
#define TOUCH_STATE_MOVE_REF4                0x0D
#define TOUCH_STATE_MOVE_REF5                0x0E
#define TOUCH_STATE_MOVE_REF6                0x0F
#define TOUCH_STATE_MOVE_REF7                0x10
#define TOUCH_STATE_MOVE_REF8                0x11

#define TOUCH_STATE_MASK                     0x1F//0x0F

#define TOUCH_STATE_MOVE_TRIGGER_POINT       0x80

//----------------------------------------------------------------------------------------------------------------------------------

#endif /* FNIRSI_101XD_SCOPE_H */

