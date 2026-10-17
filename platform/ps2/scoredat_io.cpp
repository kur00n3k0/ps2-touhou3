// PS2 port: the high score file functions the menus need
// (scoredat_load_and_decode(), scoredat_encode_and_save()), without the rest
// of the high score entry screen in th03/hiscore/regist.cpp, which belongs to
// the not yet ported MAINL side.
#include "libs/master.lib/master.hpp"
#include "th03/common.h"
#include "th03/resident.hpp"
#include "th03/formats/scoredat.hpp"

#include "th03/formats/score_ld.cpp"
#include "th03/formats/score_es.cpp"
