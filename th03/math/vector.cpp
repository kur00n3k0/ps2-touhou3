#pragma option -zCSHARED

#include "platform.h"
#include "libs/master.lib/master.hpp"
#include "th03/math/vector.hpp"

// Completely disgusting. TH02's version was fine!

#if defined(PLATFORM_PS2)
// Same math as the x86 version below: sign-extended 16-bit length times the
// sign-extended 8.8 fixed-point table entry, as a 32-bit product, then >> 8.
extern "C" const int16_t CosTable8[256];
extern "C" const int16_t SinTable8[256];

void pascal vector2(int &ret_x, int &ret_y, unsigned char angle, int length)
{
	const int32_t len = static_cast<int16_t>(length);
	ret_x = static_cast<int16_t>((len * CosTable8[angle]) >> 8);
	ret_y = static_cast<int16_t>((len * SinTable8[angle]) >> 8);
}

void pascal vector2_between_plus(
	int x1,
	int y1,
	int x2,
	int y2,
	unsigned char plus_angle,
	int &ret_x,
	int &ret_y,
	int length
)
{
	vector2(
		ret_x,
		ret_y,
		static_cast<unsigned char>(iatan2((y2 - y1), (x2 - x1)) + plus_angle),
		length
	);
}
#else
#define vector2_raw(ret_x, ret_y, angle, length) \
	_DL = angle; \
	_SI = length; \
	_EAX = static_cast<int16_t>(_SI); \
	_DH = 0; \
	_DX += _DX; /* *= sizeof(short) */ \
	_BX = _DX; \
	_asm { \
		db 0x66, 0x0F, 0xBF, 0x97;	/* MOVSX EDX,                */ \
		dw offset CosTable8;      	/*            CosTable8[bx]; */ \
		db 0x66, 0x0F, 0xBF, 0x8F;	/* MOVSX ECX,                */ \
		dw offset SinTable8;      	/*            SinTable8[bx]; */ \
	} \
	ret_x = (static_cast<long>(_EAX * _EDX) >> 8); \
	ret_y = (static_cast<long>(static_cast<int16_t>(_SI) * _ECX) >> 8);

void pascal vector2(int &ret_x, int &ret_y, unsigned char angle, int length)
{
	vector2_raw(ret_x, ret_y, angle, length);
}
#pragma codestring "\x90"

void pascal vector2_between_plus(
	int x1,
	int y1,
	int x2,
	int y2,
	unsigned char plus_angle,
	int &ret_x,
	int &ret_y,
	int length
)
{
	_SI = length;
	vector2_raw(ret_x, ret_y, (iatan2((y2 - y1), (x2 - x1)) + plus_angle), _SI);
}
#endif
